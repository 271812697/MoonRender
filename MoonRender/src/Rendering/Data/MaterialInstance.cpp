#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <format>
#include <iostream>
#include <limits>
#include <ranges>
#include <set>
#include <string_view>
#include <vector>

#include <tracy/Tracy.hpp>
#include <Rendering/Data/Material.h>
#include <Rendering/Data/MaterialBuilder.h>
#include <Rendering/HAL/UniformBuffer.h>
#include <Rendering/HAL/TextureHandle.h>
#include <Rendering/Data/MaterialParser.h>
#include <Rendering/Resources/Texture.h>

#include <Tools/Utils/OptRef.h>
#include "Core/Global/ServiceLocator.h"
#include "Core/ResourceManagement/TextureManager.h"

namespace
{
	template<typename T>
	inline void HashCombine(std::size_t& seed, const T& value)
	{
		// A large odd constant derived from the golden ratio (64-bit variant).
		// It helps distribute bits more uniformly and reduce clustering/collisions.
		constexpr std::size_t kHashCombineConstant = 0x9e3779b97f4a7c15ULL;

		seed ^= std::hash<T>{}(value)
			+kHashCombineConstant
			// Bit mixing. They spread entropy from earlier values so order matters
			// and nearby hashes don't collapse into similar outputs.
			+ (seed << 6)
			+ (seed >> 2);
	}

	Rendering::Data::MaterialPropertyType UniformToPropertyValue(const std::any& p_uniformValue)
	{
		using namespace Maths;
		using namespace Rendering;

		auto as = [&]<typename T>() -> std::optional<T> {
			return
				p_uniformValue.type() == typeid(T) ?
				std::optional<T>{std::any_cast<T>(p_uniformValue)} :
				std::nullopt;
		};

		if (auto value = as.operator() < bool > ()) return *value;
		if (auto value = as.operator() < int > ()) return *value;
		if (auto value = as.operator() < float > ()) return *value;
		if (auto value = as.operator() < FVector2 > ()) return *value;
		if (auto value = as.operator() < FVector3 > ()) return *value;
		if (auto value = as.operator() < FVector4 > ()) return *value;
		if (auto value = as.operator() < FMatrix3 > ()) return *value;
		if (auto value = as.operator() < FMatrix4 > ()) return *value;
		if (auto value = as.operator() < HAL::TextureHandle* > ()) return *value;
		if (auto value = as.operator() < Resources::Texture* > ()) return *value;

		return std::monostate{};
	}

	/** The text of a GLSL initialiser without the whitespace around it. */
	std::string_view Trimmed(std::string_view p_text)
	{
		while (!p_text.empty() && std::isspace(static_cast<unsigned char>(p_text.front())))
		{
			p_text.remove_prefix(1);
		}
		while (!p_text.empty() && std::isspace(static_cast<unsigned char>(p_text.back())))
		{
			p_text.remove_suffix(1);
		}
		return p_text;
	}

	bool ParseFloat(std::string_view p_text, float& p_value)
	{
		const std::string text(p_text);
		char* end = nullptr;
		p_value = std::strtof(text.c_str(), &end);
		return !text.empty() && end == text.c_str() + text.size();
	}

	/** The values of a GLSL constructor call: "vec4(1.0)" yields one value, "vec3(1.0, 0.5, 0)"
	 * three. The caller decides whether one value broadcasts (the language says it does). */
	bool ParseInitializerValues(std::string_view p_text, std::vector<float>& p_values)
	{
		const size_t open = p_text.find('(');
		const size_t close = p_text.rfind(')');
		if (open == std::string_view::npos || close == std::string_view::npos || close <= open)
		{
			return false;
		}

		std::string_view values = p_text.substr(open + 1, close - open - 1);
		while (!values.empty())
		{
			const size_t comma = values.find(',');
			const std::string_view token = Trimmed(values.substr(0, comma));

			float value = 0.0f;
			if (!ParseFloat(token, value))
			{
				return false;
			}
			p_values.push_back(value);

			if (comma == std::string_view::npos)
			{
				break;
			}
			values.remove_prefix(comma + 1);
		}

		return !p_values.empty();
	}

	template<size_t N>
	std::array<float, N> InitializerComponents(std::string_view p_text)
	{
		std::array<float, N> components{};

		std::vector<float> values;
		if (!ParseInitializerValues(p_text, values) || (values.size() != 1 && values.size() != N))
		{
			return components;
		}

		for (size_t i = 0; i < N; ++i)
		{
			components[i] = values[values.size() == 1 ? 0 : i];
		}
		return components;
	}

	/** Fills a row-major matrix from a GLSL constructor. "mat4(1.0)" is the diagonal matrix, and
	 * a full constructor takes its arguments column by column. */
	template<size_t N>
	void FillMatrix(float* p_data, std::string_view p_text)
	{
		std::fill(p_data, p_data + N * N, 0.0f);

		std::vector<float> values;
		if (!ParseInitializerValues(p_text, values))
		{
			return;
		}

		if (values.size() == 1)
		{
			for (size_t i = 0; i < N; ++i)
			{
				p_data[i * N + i] = values[0];
			}
		}
		else if (values.size() == N * N)
		{
			for (size_t i = 0; i < N * N; ++i)
			{
				p_data[(i % N) * N + i / N] = values[i];
			}
		}
	}

	/** The value a parameter starts with, from the initialiser the material declared.
	 *
	 * What the property model cannot hold (bvec/ivec/uvec/uint/mat2) is skipped, exactly as the
	 * driver's reflection skips it: a property no upload path understands would only turn up in
	 * the editor as a widget that does nothing. */
	template<typename Parameter>
	Rendering::Data::MaterialPropertyType ParameterDefaultValue(const Parameter& p_parameter)
	{
		using namespace Maths;
		using namespace Rendering;

		if (p_parameter.isSampler)
		{
			return static_cast<Resources::Texture*>(nullptr);
		}

		const std::string_view type = p_parameter.type;
		const std::string_view initializer = Trimmed(p_parameter.defaultValue);

		if (type == "bool")
		{
			return initializer == "true";
		}
		if (type == "int")
		{
			return static_cast<int>(std::strtol(std::string(initializer).c_str(), nullptr, 10));
		}
		if (type == "float")
		{
			float value = 0.0f;
			ParseFloat(initializer, value);
			return value;
		}
		if (type == "vec2")
		{
			const auto components = InitializerComponents<2>(initializer);
			return FVector2{ components[0], components[1] };
		}
		if (type == "vec3")
		{
			const auto components = InitializerComponents<3>(initializer);
			return FVector3{ components[0], components[1], components[2] };
		}
		if (type == "vec4")
		{
			const auto components = InitializerComponents<4>(initializer);
			return FVector4{ components[0], components[1], components[2], components[3] };
		}
		if (type == "mat3")
		{
			FMatrix3 matrix;
			FillMatrix<3>(matrix.data, initializer);
			return matrix;
		}
		if (type == "mat4")
		{
			FMatrix4 matrix;
			FillMatrix<4>(matrix.data, initializer);
			return matrix;
		}

		return std::monostate{};
	}

	/** A zero of the same kind as p_value, for a single-use property the shader does not declare
	 * (they used to be reset to the program's reflected default). */
	Rendering::Data::MaterialPropertyType ZeroOf(const Rendering::Data::MaterialPropertyType& p_value)
	{
		using namespace Maths;
		using namespace Rendering;

		return std::visit([](const auto& value) -> Rendering::Data::MaterialPropertyType {
			using T = std::decay_t<decltype(value)>;

			if constexpr (std::same_as<T, bool>) { return false; }
			else if constexpr (std::same_as<T, int>) { return 0; }
			else if constexpr (std::same_as<T, float>) { return 0.0f; }
			else if constexpr (std::same_as<T, FVector2>) { return FVector2{}; }
			else if constexpr (std::same_as<T, FVector3>) { return FVector3{}; }
			else if constexpr (std::same_as<T, FVector4>) { return FVector4{}; }
			else if constexpr (std::same_as<T, FMatrix3>) { return FMatrix3{}; }
			else if constexpr (std::same_as<T, FMatrix4>) { return FMatrix4{}; }
			else if constexpr (std::same_as<T, HAL::TextureHandle*>) { return static_cast<HAL::TextureHandle*>(nullptr); }
			else if constexpr (std::same_as<T, Resources::Texture*>) { return static_cast<Resources::Texture*>(nullptr); }
			else { return std::monostate{}; }
		}, p_value);
	}

	void BindTexture(
		Rendering::HAL::ShaderProgram& p_shader,
		const std::string& p_uniformName,
		Rendering::HAL::TextureHandle* p_texture,
		Rendering::HAL::TextureHandle* p_fallback,
		int& p_textureSlot
	)
	{
		if (auto target = p_texture ? p_texture : p_fallback)
		{
			target->Bind(p_textureSlot);
			p_shader.SetUniform<int>(p_uniformName, p_textureSlot++);
		}
	}

	/** Writes a typed value at a std140 offset. Returns false for a type/value mismatch. */
	bool WriteBlockValue(
		std::vector<uint8_t>& p_data,
		const std::string& p_type,
		const Rendering::Data::MaterialPropertyType& p_value,
		uint32_t p_offset)
	{
		using namespace Maths;

		if (p_offset >= p_data.size())
		{
			return false;
		}

		uint8_t* data = p_data.data() + p_offset;
		const auto room = [&](size_t p_size) { return p_offset + p_size <= p_data.size(); };

		if (p_type == "bool")
		{
			if (const auto* value = std::get_if<bool>(&p_value); value != nullptr && room(4))
			{
				const int32_t stored = *value ? 1 : 0;
				std::memcpy(data, &stored, sizeof(stored));
				return true;
			}
		}
		else if (p_type == "int")
		{
			if (const auto* value = std::get_if<int>(&p_value); value != nullptr && room(4))
			{
				std::memcpy(data, value, sizeof(*value));
				return true;
			}
		}
		else if (p_type == "float")
		{
			if (const auto* value = std::get_if<float>(&p_value); value != nullptr && room(4))
			{
				std::memcpy(data, value, sizeof(*value));
				return true;
			}
		}
		else if (p_type == "vec2")
		{
			if (const auto* value = std::get_if<FVector2>(&p_value); value != nullptr && room(8))
			{
				std::memcpy(data, value, sizeof(float) * 2);
				return true;
			}
		}
		else if (p_type == "vec3")
		{
			if (const auto* value = std::get_if<FVector3>(&p_value); value != nullptr && room(12))
			{
				std::memcpy(data, value, sizeof(float) * 3);
				return true;
			}
		}
		else if (p_type == "vec4")
		{
			if (const auto* value = std::get_if<FVector4>(&p_value); value != nullptr && room(16))
			{
				std::memcpy(data, value, sizeof(float) * 4);
				return true;
			}
		}
		else if (p_type == "mat3")
		{
			if (const auto* value = std::get_if<FMatrix3>(&p_value); value != nullptr && room(48))
			{
				// std140 stores a matrix as columns of a vec4; the engine's matrices are row-major.
				for (uint32_t column = 0; column < 3; ++column)
				{
					for (uint32_t row = 0; row < 3; ++row)
					{
						const float element = value->data[row * 3 + column];
						std::memcpy(data + column * 16 + row * 4, &element, sizeof(element));
					}
				}
				return true;
			}
		}
		else if (p_type == "mat4")
		{
			if (const auto* value = std::get_if<FMatrix4>(&p_value); value != nullptr && room(64))
			{
				for (uint32_t column = 0; column < 4; ++column)
				{
					for (uint32_t row = 0; row < 4; ++row)
					{
						const float element = value->data[row * 4 + column];
						std::memcpy(data + column * 16 + row * 4, &element, sizeof(element));
					}
				}
				return true;
			}
		}
		return false;
	}

	Rendering::Data::MaterialPropertyType ReadBlockValue(
		const std::vector<uint8_t>& p_data,
		const std::string& p_type,
		uint32_t p_offset)
	{
		using namespace Maths;

		const auto room = [&](size_t p_size) { return p_offset + p_size <= p_data.size(); };
		const uint8_t* data = p_data.data() + p_offset;

		if (p_type == "bool" && room(4))
		{
			int32_t stored = 0;
			std::memcpy(&stored, data, sizeof(stored));
			return stored != 0;
		}
		if (p_type == "int" && room(4))
		{
			int value = 0;
			std::memcpy(&value, data, sizeof(value));
			return value;
		}
		if (p_type == "float" && room(4))
		{
			float value = 0.0f;
			std::memcpy(&value, data, sizeof(value));
			return value;
		}
		if (p_type == "vec2" && room(8))
		{
			FVector2 value;
			std::memcpy(&value, data, sizeof(float) * 2);
			return value;
		}
		if (p_type == "vec3" && room(12))
		{
			FVector3 value;
			std::memcpy(&value, data, sizeof(float) * 3);
			return value;
		}
		if (p_type == "vec4" && room(16))
		{
			FVector4 value;
			std::memcpy(&value, data, sizeof(float) * 4);
			return value;
		}
		if (p_type == "mat3" && room(48))
		{
			FMatrix3 value;
			for (uint32_t column = 0; column < 3; ++column)
			{
				for (uint32_t row = 0; row < 3; ++row)
				{
					std::memcpy(&value.data[row * 3 + column], data + column * 16 + row * 4, sizeof(float));
				}
			}
			return value;
		}
		if (p_type == "mat4" && room(64))
		{
			FMatrix4 value;
			for (uint32_t column = 0; column < 4; ++column)
			{
				for (uint32_t row = 0; row < 4; ++row)
				{
					std::memcpy(&value.data[row * 4 + column], data + column * 16 + row * 4, sizeof(float));
				}
			}
			return value;
		}
		return std::monostate{};
	}

	uint32_t BlockFieldSize(const std::string& p_type)
	{
		if (p_type == "bool" || p_type == "int" || p_type == "float") { return 4; }
		if (p_type == "vec2") { return 8; }
		if (p_type == "vec3") { return 12; }
		if (p_type == "vec4") { return 16; }
		if (p_type == "mat3") { return 48; }
		if (p_type == "mat4") { return 64; }
		return 0;
	}

	/** Splits "samples[17]" into its base name and element index; no brackets means index 0. */
	bool ParseParameterName(const std::string& p_name, std::string& p_base, uint32_t& p_index)
	{
		p_base = p_name;
		p_index = 0;
		const size_t open = p_name.rfind('[');
		if (open == std::string::npos || p_name.back() != ']')
		{
			return true;
		}
		const std::string_view indexText(p_name.data() + open + 1,
			p_name.size() - open - 2);
		if (indexText.empty())
		{
			return false;
		}
		uint32_t index = 0;
		for (const char c : indexText)
		{
			if (!std::isdigit(static_cast<unsigned char>(c)))
			{
				return false;
			}
			index = index * 10 + static_cast<uint32_t>(c - '0');
		}
		p_base = p_name.substr(0, open);
		p_index = index;
		return true;
	}

	const Rendering::Data::MaterialDefinition::Parameter* FindParameter(
		const Rendering::Data::MaterialDefinition* p_definition,
		const std::string& p_name,
		uint32_t& p_index)
	{
		std::string base;
		if (p_definition == nullptr || !ParseParameterName(p_name, base, p_index))
		{
			return nullptr;
		}
		for (const Rendering::Data::MaterialDefinition::Parameter& parameter : p_definition->Parameters())
		{
			if (parameter.name == base)
			{
				return &parameter;
			}
		}
		return nullptr;
	}
}


Rendering::Data::MaterialInstance::MaterialInstance(Material& p_definition, bool p_isDefault)
	: m_definition(&p_definition)
	, m_isDefaultInstance(p_isDefault)
{
	// A plain instance starts empty and inherits; the default instance is the definition's
	// own value set, seeded from the engine defaults here and from the shader in ResetProperties.
	if (p_isDefault)
	{
		InitializeDefaultState();
	}
}

Rendering::Data::MaterialInstance::MaterialInstance()
{
	InitializeDefaultState();
}

void Rendering::Data::MaterialInstance::InitializeDefaultState()
{
	m_supportOrthographic = true;
	m_supportPerspective = true;
	m_userInterface = false;
	m_blendable = false;
	m_blendMode = Rendering::Settings::EBlendMode::BLEND_OPAQUE;
	m_transparent = false;
	m_backfaceCulling = true;
	m_frontfaceCulling = false;
	m_polygonOffsetFill = false;
	m_depthTest = true;
	m_depthWriting = true;
	m_colorWriting = true;
	m_castShadows = true;
	m_receiveShadows = true;
	m_capturedByReflectionProbes = true;
	m_receiveReflections = true;
	m_gpuInstances = 1;
	m_drawOrder = 1000;
	m_lineWidth = 1.0f;
}

void Rendering::Data::MaterialInstance::SetMaterial(Material& p_material)
{
	m_definition = &p_material;
	m_isDefaultInstance = false;
	m_overriddenUniforms.clear();
	m_overriddenSamplers.clear();
	m_singleUseUniforms.clear();
	m_singleUseSamplers.clear();
	m_looseUniforms.clear();
	ReleaseDescriptorSet();
	ResetStateToMaterial();
	ResetProperties();
	++m_revision;
}

void Rendering::Data::MaterialInstance::ResetStateToMaterial()
{
	InitializeDefaultState();

	if (m_definition != nullptr && m_definition->HasState())
	{
		const Material::State& state = m_definition->GetState();
		if (state.blendable.has_value()) { m_blendable = state.blendable.value(); }
		if (state.blendMode.has_value())
		{
			m_blendMode = state.blendMode.value();
			m_blendable = state.blendMode.value() != Rendering::Settings::EBlendMode::BLEND_OPAQUE;
		}
		if (state.backfaceCulling.has_value()) { m_backfaceCulling = state.backfaceCulling.value(); }
		if (state.frontfaceCulling.has_value()) { m_frontfaceCulling = state.frontfaceCulling.value(); }
		if (state.depthWriting.has_value()) { m_depthWriting = state.depthWriting.value(); }
	}
}


Rendering::Data::MaterialInstance::~MaterialInstance()
{
	ReleaseDescriptorSet();
}


Rendering::Data::MaterialInstance::LooseUniform*
Rendering::Data::MaterialInstance::FindLooseUniform(const std::string& p_name)
{
	for (LooseUniform& uniform : m_looseUniforms)
	{
		if (uniform.name == p_name)
		{
			return &uniform;
		}
	}
	return nullptr;
}


const Rendering::Data::MaterialInstance::LooseUniform*
Rendering::Data::MaterialInstance::FindLooseUniform(const std::string& p_name) const
{
	for (const LooseUniform& uniform : m_looseUniforms)
	{
		if (uniform.name == p_name)
		{
			return &uniform;
		}
	}
	return nullptr;
}


std::map<std::string, Rendering::Data::MaterialProperty>
Rendering::Data::MaterialInstance::SnapshotProperties() const
{
	std::map<std::string, MaterialProperty> snapshot;
	const MaterialDefinition* definition = Definition();
	if (definition == nullptr)
	{
		return snapshot;
	}

	for (const MaterialDefinition::Parameter& parameter : definition->Parameters())
	{
		if (parameter.count > 1 && !parameter.isSampler)
		{
			for (uint32_t i = 0; i < parameter.count; ++i)
			{
				const std::string name = std::format("{}[{}]", parameter.name, i);
				if (auto value = GetProperty(name))
				{
					snapshot.emplace(name, std::move(*value));
				}
			}
		}
		else if (auto value = GetProperty(parameter.name))
		{
			snapshot.emplace(parameter.name, std::move(*value));
		}
	}

	for (const LooseUniform& uniform : m_looseUniforms)
	{
		snapshot.emplace(uniform.name, MaterialProperty{ uniform.value, uniform.singleUse });
	}

	return snapshot;
}


std::optional<Rendering::Data::MaterialProperty>
Rendering::Data::MaterialInstance::GetProperty(const std::string p_key) const
{
	const MaterialDefinition* definition = Definition();
	uint32_t index = 0;
	const MaterialDefinition::Parameter* parameter =
		definition != nullptr ? FindParameter(definition, p_key, index) : nullptr;

	if (parameter != nullptr)
	{
		EnsureDescriptorSet();
		if (parameter->isSampler)
		{
			// The descriptor keeps both the GL handle and the serializable asset. Reporting the
			// asset first lets a .mat write the path it came from; a view-owned texture has no
			// asset, so the handle is what the editor sees.
			if (auto* asset = m_descriptorSet.GetTextureAsset(parameter->binding))
			{
				return MaterialProperty{ asset, false };
			}
			if (auto* handle = m_descriptorSet.GetTexture(parameter->binding))
			{
				return MaterialProperty{ handle, false };
			}
			return MaterialProperty{ static_cast<Rendering::Resources::Texture*>(nullptr), false };
		}

		EnsureDescriptorSet();
		const MaterialDefinition::UniformField* field = definition->FindUniform(parameter->name);
		if (field != nullptr && m_uniformData.size() == definition->ParameterBlockSize())
		{
			const uint32_t offset = field->offset + (field->count > 1 ? field->stride * index : 0);
			return MaterialProperty{
				ReadBlockValue(m_uniformData, field->type, offset),
				false
			};
		}
	}

	if (const LooseUniform* uniform = FindLooseUniform(p_key))
	{
		return MaterialProperty{ uniform->value, uniform->singleUse };
	}
	return std::nullopt;
}

Rendering::Data::Material* Rendering::Data::MaterialInstance::ShaderOf() const
{
	return m_definition != nullptr ? m_definition->GetShader() : nullptr;
}


const Rendering::Data::MaterialDefinition* Rendering::Data::MaterialInstance::Definition() const
{
	return m_definition != nullptr ? m_definition->Definition() : nullptr;
}


Tools::Utils::OptRef<Rendering::HAL::ShaderProgram> Rendering::Data::MaterialInstance::GetVariant(
	std::optional<const std::string_view> p_pass,
	Tools::Utils::OptRef<const Data::FeatureSet> p_override
) const
{
	if (ShaderOf() != nullptr)
	{
		auto& program = ShaderOf()->GetVariant(
			p_pass,
			p_override.value_or(FeaturesOf())
		);
		if (Definition() != nullptr)
		{
			program.InitializeDescriptorBindings(Definition()->ProgramBindings());
		}
		return program;
	}

	return std::nullopt;
}


void Rendering::Data::MaterialInstance::UploadProperties(bool uploadStableProperties, bool uploadSingleUseProperties, Rendering::HAL::Texture* p_emptyTexture2D, Rendering::HAL::Texture* p_emptyTextureCube, Rendering::HAL::Texture* p_brdfLut, Rendering::HAL::Texture* p_flatNormalTexture)
{
	ZoneScoped;

	// Inherited values: when the definition changed since the last upload, this instance's cached
	// signature describes stale data (the block would keep the old numbers), so invalidate it.
	if (m_definition != nullptr && m_definition->Revision() != m_seenDefinitionRevision)
	{
		m_seenDefinitionRevision = m_definition->Revision();
		InvalidatePropertySignature();
	}

	// Packaged shaders carry their parameters as one std140 block (the per-material descriptor):
	// write the values by offset into it and bind it once, instead of resolving each name
	// against the program's reflected uniforms.
	const MaterialDefinition* definition = Definition();
	const bool hasDefinitionParameters = definition != nullptr && definition->HasParameterTable();
	if (hasDefinitionParameters)
	{
		UploadParameterBlock(uploadStableProperties, uploadSingleUseProperties, p_emptyTexture2D, p_emptyTextureCube, p_brdfLut, p_flatNormalTexture);
	}

	/** The 1x1 texture a sampler gets when the material instance has none: the material's own
	 * `fallback :` kind when the package declared one, the engine's defaults otherwise. */
	const auto fallbackTexture = [definition, p_emptyTexture2D, p_emptyTextureCube, p_flatNormalTexture](
		const std::string& p_name, bool p_isCube) -> Rendering::HAL::Texture*
	{
		if (definition != nullptr)
		{
			for (const MaterialDefinition::Parameter& parameter : definition->Parameters())
			{
				if (parameter.isSampler && parameter.name == p_name
					&& parameter.fallback == static_cast<uint8_t>(Rendering::Data::MaterialParser::SamplerFallback::FlatNormal)
					&& p_flatNormalTexture != nullptr)
				{
					return p_flatNormalTexture;
				}
			}
		}
		return p_isCube ? p_emptyTextureCube : p_emptyTexture2D;
	};

	// Properties the shader does not declare (an include's own uniforms, like PBR.glsl's
	// u_ReceiveShadows) are not in the block: they still go through the per-name path below,
	// while the declared ones were written by UploadParameterBlock.
	const auto isDeclaredParameter = [definition](const std::string& p_name) {
		if (definition == nullptr)
		{
			return false;
		}
		for (const MaterialDefinition::Parameter& parameter : definition->Parameters())
		{
			if (parameter.name == p_name || p_name.starts_with(parameter.name + "["))
			{
				return true;
			}
		}
		return false;
	};

	using namespace Maths;
	using enum Rendering::Settings::EUniformType;
	auto& program = m_programInUse.value();

	// Loose uniforms of a source-built shader; the program's reflected uniform locations decide
	// how each typed value is uploaded.
	std::map<std::string, MaterialProperty> looseProperties;
	for (const LooseUniform& uniform : m_looseUniforms)
	{
		looseProperties.emplace(uniform.name,
			MaterialProperty{ uniform.value, uniform.singleUse });
	}

	int textureSlot = 0;
	for (auto& [name, prop] : looseProperties)
	{
		if (!uploadStableProperties && !prop.singleUse) continue;
		if (!uploadSingleUseProperties && prop.singleUse) continue;
		if (hasDefinitionParameters && isDeclaredParameter(name)) continue;

		const auto uniformData = program.GetUniformInfo(name);

		// Skip this property if the current program isn't using its associated uniform
		if (!uniformData)
		{
			continue;
		}

		auto& value = prop.value;
		auto uniformType = uniformData.value().type;

		// Iterating over the properties to set them in the shader.
		// This could have been cleaner with a visitor, but the performance impact
		// is not worth it. This is a critical path in the rendering pipeline.

		if (uniformType == BOOL)
		{
			program.SetUniform<int>(name, static_cast<int>(std::get<bool>(value)));
		}
		else if (uniformType == INT)
		{
			program.SetUniform<int>(name, std::get<int>(value));
		}
		else if (uniformType == FLOAT)
		{
			program.SetUniform<float>(name, std::get<float>(value));
		}
		else if (uniformType == FLOAT_VEC2)
		{
			program.SetUniform<FVector2>(name,std::get<FVector2>(value));
		}
		else if (uniformType == FLOAT_VEC3)
		{
			program.SetUniform<FVector3>(name, std::get<FVector3>(value));
		}
		else if (uniformType == FLOAT_VEC4)
		{
			program.SetUniform<FVector4>(name, std::get<FVector4>(value));
		}
		else if (uniformType == FLOAT_MAT3)
		{
			// No transpose here: SetUniform<FMatrix4/3> uploads with the
			// "transpose" flag set, i.e. it already expects row-major data,
			// which is exactly how Maths::FMatrix4 stores it. Transposing again
			// would flip every matrix property.
			program.SetUniform<FMatrix3>(name, std::get<FMatrix3>(value));
		}
		else if (uniformType == FLOAT_MAT4)
		{
			program.SetUniform<FMatrix4>(name, std::get<FMatrix4>(value));
		}
		else if (
			uniformType == SAMPLER_2D || uniformType == SAMPLER_CUBE
			|| uniformType == SAMPLER_BUFFER || uniformType == UINTSAMPLER_BUFFER
			|| uniformType == SAMPLER_2DARRAY || uniformType == INTSAMPLER_BUFFER
			)
		{
			HAL::TextureHandle* handle = nullptr;
			if (auto textureHandle = std::get_if<HAL::TextureHandle*>(&value))
			{
				handle = *textureHandle;
			}
			else if (auto texture = std::get_if<Resources::Texture*>(&value))
			{
				if (*texture != nullptr)
				{
					handle = &(*texture)->GetTexture();
				}
			}
			BindTexture(program, name, handle,
				fallbackTexture(name, uniformType != SAMPLER_2D && uniformType != SAMPLER_BUFFER),
				textureSlot);
		}

		if (prop.singleUse)
		{
			value = UniformToPropertyValue(uniformData->defaultValue);
			if (LooseUniform* stored = FindLooseUniform(name))
			{
				stored->value = value;
			}
		}
	}
}


void Rendering::Data::MaterialInstance::EnsureDescriptorSet() const
{
	const MaterialDefinition* definition = Definition();
	if (definition == nullptr)
	{
		const_cast<MaterialInstance*>(this)->ReleaseDescriptorSet();
		return;
	}

	DescriptorSet& set = const_cast<DescriptorSet&>(m_descriptorSet);
	MaterialInstance& self = *const_cast<MaterialInstance*>(this);

	// The layout is the definition's, not the instance's. A hot reload that changes the
	// layout starts the set over.
	if (set.Layout() != &definition->Layout())
	{
		self.ReleaseDescriptorSet();
		set.SetLayout(&definition->Layout());
	}

	const uint32_t blockSize = definition->ParameterBlockSize();
	if (blockSize > 0)
	{
		if (m_uniformData.size() != blockSize)
		{
			m_uniformData.assign(blockSize, 0);
			self.ResetBlockToDefinition();
		}
		if (!m_uniformSlice.IsValid() || m_uniformSlice.size != blockSize)
		{
			UniformBufferPool& pool = UniformBufferPool::Get();
			if (m_uniformSlice.IsValid())
			{
				pool.Release(m_uniformSlice);
			}
			m_uniformSlice = pool.Acquire(blockSize);
			const DescriptorSetLayout::Binding* uniform =
				definition->Layout().Find("MaterialParams");
			set.SetBuffer(uniform != nullptr ? uniform->binding : 0,
				Descriptor::BufferType::Uniform, pool.GetBufferId(),
				m_uniformSlice.offset, m_uniformSlice.size);
		}
	}
	else
	{
		m_uniformData.clear();
		if (m_uniformSlice.IsValid())
		{
			UniformBufferPool::Get().Release(m_uniformSlice);
			m_uniformSlice = {};
		}
	}

	// A plain instance inherits everything it does not override from the material's default
	// instance. This runs on every upload/bind, not only once: a .mat value or a per-frame
	// property set on the default instance after this instance was created must still arrive.
	self.MergeDefaults();
}


void Rendering::Data::MaterialInstance::ReleaseDescriptorSet()
{
	if (m_uniformSlice.IsValid())
	{
		UniformBufferPool::Get().Release(m_uniformSlice);
		m_uniformSlice = {};
	}
	m_uniformData.clear();
	m_descriptorSet.Release();
}

void Rendering::Data::MaterialInstance::BindParameterBlock(
	const Rendering::HAL::ShaderProgram& p_program)
{
	EnsureDescriptorSet();
	MergeDefaults();

	// Per-material descriptor set (filament's PER_MATERIAL): the array is indexed by the
	// normalized binding point, and the program's binding map says which GL slot it got.
	m_descriptorSet.Bind(p_program, kPerMaterialDescriptorSet);
}


void Rendering::Data::MaterialInstance::ResetBlockToDefinition()
{
	const MaterialDefinition* definition = Definition();
	if (definition == nullptr || definition->ParameterBlockSize() == 0)
	{
		return;
	}

	m_uniformData.assign(definition->ParameterBlockSize(), 0);
	std::vector<uint8_t>& block = m_uniformData;

	for (const MaterialDefinition::UniformField& field : definition->UniformFields())
	{
		const auto parameter = std::ranges::find_if(definition->Parameters(),
			[&field](const MaterialDefinition::Parameter& p_parameter) {
				return p_parameter.name == field.name;
			});
		if (parameter == definition->Parameters().end())
		{
			continue;
		}

		const MaterialPropertyType value = ParameterDefaultValue(*parameter);
		if (std::holds_alternative<std::monostate>(value))
		{
			continue;
		}

		if (field.count > 1)
		{
			for (uint32_t i = 0; i < field.count; ++i)
			{
				WriteBlockValue(block, field.type, value, field.offset + field.stride * i);
			}
		}
		else
		{
			WriteBlockValue(block, field.type, value, field.offset);
		}
	}
}


void Rendering::Data::MaterialInstance::MergeDefaults()
{
	if (m_isDefaultInstance || m_definition == nullptr)
	{
		return;
	}

	const MaterialDefinition* definition = Definition();
	if (definition == nullptr)
	{
		return;
	}

	const MaterialInstance& defaults = m_definition->DefaultInstance();
	const uint32_t blockSize = definition->ParameterBlockSize();
	if (blockSize > 0 && defaults.BufferSize() == blockSize)
	{
		if (m_uniformData.size() != blockSize)
		{
			m_uniformData.assign(blockSize, 0);
		}
		std::vector<uint8_t>& block = m_uniformData;
		const std::vector<uint8_t>& inherited = defaults.BufferData();
		for (const MaterialDefinition::UniformField& field : definition->UniformFields())
		{
			if (m_overriddenUniforms.contains(field.name))
			{
				continue;
			}

			const uint32_t elementSize = BlockFieldSize(field.type);
			if (elementSize == 0)
			{
				continue;
			}
			const uint32_t count = field.count > 0 ? field.count : 1;
			for (uint32_t i = 0; i < count; ++i)
			{
				const uint32_t offset = field.offset + field.stride * i;
				if (offset + elementSize <= block.size() && offset + elementSize <= inherited.size())
				{
					std::memcpy(block.data() + offset, inherited.data() + offset, elementSize);
				}
			}
		}
	}

	for (const MaterialDefinition::SamplerField& field : definition->SamplerFields())
	{
		if (m_overriddenSamplers.contains(field.binding))
		{
			continue;
		}
		Rendering::HAL::TextureHandle* handle = defaults.GetTexture(field.binding);
		Rendering::Resources::Texture* asset = defaults.GetTextureAsset(field.binding);
		if (handle != nullptr || asset != nullptr)
		{
			m_descriptorSet.SetTexture(field.binding, handle, asset);
		}
	}
}


void Rendering::Data::MaterialInstance::ResetSingleUseProperties()
{
	const MaterialDefinition* definition = Definition();
	if (definition == nullptr)
	{
		m_singleUseUniforms.clear();
		m_singleUseSamplers.clear();
		return;
	}

	if (!m_singleUseUniforms.empty() && definition->ParameterBlockSize() > 0)
	{
		if (m_uniformData.size() != definition->ParameterBlockSize())
		{
			m_uniformData.assign(definition->ParameterBlockSize(), 0);
		}
		std::vector<uint8_t>& block = m_uniformData;
		for (const std::string& name : m_singleUseUniforms)
		{
			uint32_t index = 0;
			const MaterialDefinition::Parameter* parameter = FindParameter(definition, name, index);
			if (parameter == nullptr || parameter->isSampler)
			{
				continue;
			}
			const MaterialPropertyType value = ParameterDefaultValue(*parameter);
			if (std::holds_alternative<std::monostate>(value))
			{
				continue;
			}
			const MaterialDefinition::UniformField* field = definition->FindUniform(parameter->name);
			const uint32_t offset = field != nullptr
				? field->offset + (field->count > 1 ? field->stride * index : 0)
				: static_cast<uint32_t>(std::max(parameter->offset, 0));
			WriteBlockValue(block, parameter->type, value, offset);
			m_overriddenUniforms.erase(parameter->name);
		}
	}

	for (const uint32_t binding : m_singleUseSamplers)
	{
		Rendering::HAL::TextureHandle* handle = nullptr;
		Rendering::Resources::Texture* asset = nullptr;
		if (!m_isDefaultInstance && m_definition != nullptr)
		{
			handle = m_definition->DefaultInstance().GetTexture(binding);
			asset = m_definition->DefaultInstance().GetTextureAsset(binding);
		}
		m_descriptorSet.SetTexture(binding, handle, asset);
		m_overriddenSamplers.erase(binding);
	}

	m_singleUseUniforms.clear();
	m_singleUseSamplers.clear();
	InvalidatePropertySignature();
}


void Rendering::Data::MaterialInstance::UploadParameterBlock(
	bool p_uploadStableProperties,
	bool p_uploadSingleUseProperties,
	Rendering::HAL::Texture* p_emptyTexture2D,
	Rendering::HAL::Texture* p_emptyTextureCube,
	Rendering::HAL::Texture* p_brdfLut,
	Rendering::HAL::Texture* p_flatNormalTexture)
{
	const MaterialDefinition* definition = Definition();
	if (definition == nullptr)
	{
		return;
	}

	EnsureDescriptorSet();

	// The block values are written by SetProperty() directly into m_uniformData at the
	// definition's std140 offset. The descriptor's GL range is the GPU half; upload here and
	// keep the pool's reallocation mirror in sync.
	if (definition->ParameterBlockSize() > 0 && m_uniformSlice.IsValid()
		&& m_uniformData.size() >= definition->ParameterBlockSize())
	{
		const DescriptorSetLayout::Binding* uniform =
			definition->Layout().Find("MaterialParams");
		m_descriptorSet.UploadBuffer(
			uniform != nullptr ? uniform->binding : 0,
			Descriptor::BufferType::Uniform, m_uniformData.data(),
			definition->ParameterBlockSize());
		UniformBufferPool::Get().StoreCPU(m_uniformSlice, m_uniformData.data());
	}

	// Samplers: one fixed texture unit per declaration, written into the package by MoonMatc
	// next to the block. The definition's sampler fields carry both the unit and the fallback.
	for (const MaterialDefinition::SamplerField& field : definition->SamplerFields())
	{
		Rendering::HAL::TextureHandle* handle = m_descriptorSet.GetTexture(field.binding);
		Rendering::Resources::Texture* asset = m_descriptorSet.GetTextureAsset(field.binding);
		if (handle == nullptr && asset != nullptr)
		{
			handle = &asset->GetTexture();
		}

		// The split-sum DFG LUT is global to the view, like Filament's frame uniform. Binding it
		// through the material property was fragile: an instance without the parameter (the
		// material preview) sampled the white empty 2D, giving DFG=(1,1), E~=1 and no diffuse.
		if (field.name == "_BRDFLut" && p_brdfLut != nullptr && p_brdfLut->IsValid())
		{
			m_descriptorSet.SetTexture(field.binding, p_brdfLut);
			continue;
		}

		// A buffer texture has no 1x1 stand-in in the engine; binding the 2D fallback to a
		// samplerBuffer slot is a type error at draw time. An unassigned one is simply left
		// unbound (the shader that declares it does not read it without a buffer).
		if (handle == nullptr && field.type != "sampler2D" && field.type != "samplerCube")
		{
			m_descriptorSet.SetTexture(field.binding, nullptr);
			continue;
		}

		Rendering::HAL::TextureHandle* fallback =
			field.type == "samplerCube" ? p_emptyTextureCube : p_emptyTexture2D;
		const char* fallbackName = field.type == "samplerCube" ? "empty cube (black)" : "empty 2D (white)";
		if (field.fallback == static_cast<uint8_t>(Rendering::Data::MaterialParser::SamplerFallback::FlatNormal)
			&& p_flatNormalTexture != nullptr)
		{
			fallback = p_flatNormalTexture;
			fallbackName = "flat normal (0.5,0.5,1)";
		}

		m_descriptorSet.SetTexture(field.binding, handle != nullptr ? handle : fallback,
			handle != nullptr ? asset : nullptr);

		// MOON_DEBUG_SAMPLERS: what each sampler slot actually got, once per shader and slot.
		// The "why is my texture ignored" question has two very different answers (the material
		// property is empty vs. it is set but the shader never reads it), and this says which.
		if (std::getenv("MOON_DEBUG_SAMPLERS") != nullptr)
		{
			static std::set<std::string> reported;
			const std::string key = (ShaderOf() != nullptr ? ShaderOf()->path : std::string("?")) + "|" + field.name;
			if (reported.insert(key).second)
			{
				std::cout << "[Sampler] " << (ShaderOf() != nullptr ? ShaderOf()->path : "?") << " "
					<< field.name << " unit " << field.binding << " <- "
					<< (handle != nullptr ? "material texture" : fallbackName) << std::endl;
			}
		}
	}

	// Single-use values went to the GPU above; put the CPU side back to the default so the
	// next draw that does not set them does not inherit this frame's scratch values.
	ResetSingleUseProperties();
}


void Rendering::Data::MaterialInstance::BindGlobalTexture(const std::string& p_name, Rendering::HAL::Texture* p_texture)
{
	// A view-owned texture (the DFG LUT, the environment cubes) is allocated by an earlier
	// pass; on the first frames it may not exist yet, and binding an unallocated name is a GL
	// error rather than a no-op.
	if (Definition() == nullptr || p_texture == nullptr || !p_texture->IsValid())
	{
		return;
	}

	if (const MaterialDefinition::SamplerField* field = Definition()->FindSampler(p_name))
	{
		// Remember it in the set as well: the next Bind() must not put back whatever the
		// material's own upload recorded for this slot.
		m_descriptorSet.SetTexture(field->binding, p_texture);
		m_overriddenSamplers.insert(field->binding);
		if (m_programInUse)
		{
			m_descriptorSet.Bind(m_programInUse.value(), kPerMaterialDescriptorSet);
		}

		// MOON_DEBUG_SAMPLERS also reports the per-draw bindings: the ones in UploadParameterBlock
		// happen once (and may have run before the sky pass baked the environment), while these
		// happen on every draw and are what the shader really samples.
		if (std::getenv("MOON_DEBUG_SAMPLERS") != nullptr)
		{
			static std::set<std::string> reported;
			const std::string key = ShaderOf()->path + "|draw|" + field->name;
			if (reported.insert(key).second)
			{
				std::cout << "[Sampler] " << ShaderOf()->path << " " << field->name << " unit "
					<< field->binding << " <- view texture (per draw)" << std::endl;
			}
		}
	}
}


void Rendering::Data::MaterialInstance::SetLooseUniform(
	const std::string& p_name,
	const MaterialPropertyType& p_value,
	bool p_singleUse)
{
	if (LooseUniform* uniform = FindLooseUniform(p_name))
	{
		uniform->value = p_value;
		uniform->singleUse = p_singleUse;
	}
	else
	{
		m_looseUniforms.push_back(LooseUniform{ p_name, p_value, p_singleUse });
	}
}


bool Rendering::Data::MaterialInstance::ResetPropertyToDefault(const std::string& p_name)
{
	const MaterialDefinition* definition = Definition();
	if (definition == nullptr)
	{
		if (FindLooseUniform(p_name) != nullptr)
		{
			std::erase_if(m_looseUniforms, [&p_name](const LooseUniform& p_uniform) {
				return p_uniform.name == p_name;
			});
			++m_revision;
			InvalidatePropertySignature();
			return true;
		}
		return false;
	}

	uint32_t index = 0;
	const MaterialDefinition::Parameter* parameter = FindParameter(definition, p_name, index);
	if (parameter == nullptr)
	{
		if (FindLooseUniform(p_name) != nullptr)
		{
			std::erase_if(m_looseUniforms, [&p_name](const LooseUniform& p_uniform) {
				return p_uniform.name == p_name;
			});
			++m_revision;
			InvalidatePropertySignature();
			return true;
		}
		return false;
	}

	EnsureDescriptorSet();
	if (parameter->isSampler)
	{
		Rendering::HAL::TextureHandle* handle = nullptr;
		Rendering::Resources::Texture* asset = nullptr;
		if (!m_isDefaultInstance && m_definition != nullptr)
		{
			handle = m_definition->DefaultInstance().GetTexture(parameter->binding);
			asset = m_definition->DefaultInstance().GetTextureAsset(parameter->binding);
		}
		m_descriptorSet.SetTexture(parameter->binding, handle, asset);
		m_overriddenSamplers.erase(parameter->binding);
		++m_revision;
		InvalidatePropertySignature();
		return true;
	}

	const MaterialPropertyType value = ParameterDefaultValue(*parameter);
	if (std::holds_alternative<std::monostate>(value))
	{
		return false;
	}

	const MaterialDefinition::UniformField* field = definition->FindUniform(parameter->name);
	if (m_uniformData.size() != definition->ParameterBlockSize())
	{
		m_uniformData.assign(definition->ParameterBlockSize(), 0);
	}
	std::vector<uint8_t>& block = m_uniformData;
	const uint32_t offset = field != nullptr
		? field->offset + (field->count > 1 ? field->stride * index : 0)
		: static_cast<uint32_t>(std::max(parameter->offset, 0));
	WriteBlockValue(block, parameter->type, value, offset);
	m_overriddenUniforms.erase(parameter->name);
	++m_revision;
	InvalidatePropertySignature();
	return true;
}


void Rendering::Data::MaterialInstance::ResetProperties()
{
	InvalidatePropertySignature();
	m_overriddenUniforms.clear();
	m_overriddenSamplers.clear();
	m_singleUseUniforms.clear();
	m_singleUseSamplers.clear();
	m_looseUniforms.clear();
	ReleaseDescriptorSet();
	EnsureDescriptorSet();
	++m_revision;
}


void Rendering::Data::MaterialInstance::UpdateProperties()
{
	ResetProperties();
}


Rendering::Data::MaterialSignatureSet Rendering::Data::MaterialInstance::Bind(
	HAL::Texture* p_emptyTexture,
	HAL::Texture* p_emptyTextureCube,
	std::optional<const std::string_view> p_pass,
	Tools::Utils::OptRef<const Data::FeatureSet> p_featureSetOverride,
	std::optional<MaterialSignatureSet> p_previousMaterialSignature 
)
{
	ZoneScoped;
	auto& program = ShaderOf()->GetVariant(
		p_pass,
		p_featureSetOverride.value_or(FeaturesOf())
	);
	if (Definition() != nullptr)
	{
		program.InitializeDescriptorBindings(Definition()->ProgramBindings());
	}

	const auto signature = CalculateSignature(
		program,
		p_emptyTexture,
		p_emptyTextureCube
	);

	if (!p_previousMaterialSignature || signature.bindSignature != p_previousMaterialSignature->bindSignature)
	{
		program.Bind();
	}

	m_programInUse = program;

	// Loose uniforms (a shader built without a package) are descriptors too: apply them to the
	// program here, before the material's block and samplers are bound.

	// The parameter block is per material, not per program: bind it on every draw so it is the
	// one bound even when another material uploaded its own buffer since the last draw.
	BindParameterBlock(program);

	return signature;
}


void Rendering::Data::MaterialInstance::Unbind(bool p_resetBoundProgram)
{
	if (p_resetBoundProgram)
	{
		m_programInUse->Unbind();
	}

	m_programInUse.reset();
}


bool Rendering::Data::MaterialInstance::HasProperty(const std::string& p_name) const
{
	const MaterialDefinition* definition = Definition();
	uint32_t index = 0;
	return (definition != nullptr && FindParameter(definition, p_name, index) != nullptr)
		|| FindLooseUniform(p_name) != nullptr;
}


void Rendering::Data::MaterialInstance::SetProperty(const std::string p_name, const MaterialPropertyType& p_value, bool p_singleUse)
{
	const MaterialDefinition* definition = Definition();
	uint32_t index = 0;
	const MaterialDefinition::Parameter* parameter =
		definition != nullptr ? FindParameter(definition, p_name, index) : nullptr;

	if (parameter != nullptr)
	{
		EnsureDescriptorSet();
		if (parameter->isSampler)
		{
			Rendering::HAL::TextureHandle* handle = nullptr;
			Rendering::Resources::Texture* asset = nullptr;
			if (const auto* value = std::get_if<Rendering::HAL::TextureHandle*>(&p_value))
			{
				handle = *value;
			}
			else if (const auto* value = std::get_if<Rendering::Resources::Texture*>(&p_value))
			{
				asset = *value;
				handle = asset != nullptr ? &asset->GetTexture() : nullptr;
			}
			m_descriptorSet.SetTexture(parameter->binding, handle, asset);
			m_overriddenSamplers.insert(parameter->binding);
			if (p_singleUse) { m_singleUseSamplers.insert(parameter->binding); }
		}
		else
		{
			const MaterialDefinition::UniformField* field = definition->FindUniform(parameter->name);
			if (m_uniformData.size() != definition->ParameterBlockSize())
			{
				m_uniformData.assign(definition->ParameterBlockSize(), 0);
			}
			std::vector<uint8_t>& block = m_uniformData;
			const uint32_t offset = field != nullptr
				? field->offset + (field->count > 1 ? field->stride * index : 0)
				: static_cast<uint32_t>(std::max(parameter->offset, 0));
			WriteBlockValue(block, parameter->type, p_value, offset);
			m_overriddenUniforms.insert(parameter->name);
			if (p_singleUse) { m_singleUseUniforms.insert(p_name); }
		}
	}
	else
	{
		// A shader built without a package has no block slot for this uniform; keep the loose
		// value and upload it through the program's uniform location.
		SetLooseUniform(p_name, p_value, p_singleUse);
	}

	++m_revision;

	if (p_singleUse)
	{
		++m_singleUsePropertySignatureVersion;
	}
	else
	{
		++m_stablePropertySignatureVersion;
	}
}


void Rendering::Data::MaterialInstance::LateUpdateTexture(const std::string& p_name, const std::string& p_path)
{
	(void)p_path;
	if (HasProperty(p_name))
	{
		++m_revision;
	}
}


bool Rendering::Data::MaterialInstance::TrySetProperty(const std::string& p_name, const MaterialPropertyType& p_value, bool p_singleUse)
{
	if (HasProperty(p_name))
	{
		SetProperty(p_name, p_value, p_singleUse);
		return true;
	}
	return false;
}


bool Rendering::Data::MaterialInstance::IsValid() const
{
	return ShaderOf() != nullptr;
}


void Rendering::Data::MaterialInstance::SetOrthographicSupport(bool p_supportOrthographic)
{
	m_supportOrthographic = p_supportOrthographic;
}


void Rendering::Data::MaterialInstance::SetPerspectiveSupport(bool p_supportPerspective)
{
	m_supportPerspective = p_supportPerspective;
}


void Rendering::Data::MaterialInstance::SetDrawOrder(int p_order)
{
	m_drawOrder = p_order;
}


void Rendering::Data::MaterialInstance::SetBlendable(bool p_transparent)
{
	m_blendable = p_transparent;

	// Keep the mode in step: the renderer derives its blend factors from it, and an alpha
	// material that only flips "blendable" still means the standard alpha blend.
	if (p_transparent && GetBlendMode() == Rendering::Settings::EBlendMode::BLEND_OPAQUE)
	{
		m_blendMode = Rendering::Settings::EBlendMode::BLEND_TRANSPARENT;
	}
	else if (!p_transparent)
	{
		m_blendMode = Rendering::Settings::EBlendMode::BLEND_OPAQUE;
	}
}


void Rendering::Data::MaterialInstance::SetBlendMode(Rendering::Settings::EBlendMode p_blendMode)
{
	// The blend mode is about factors only. Queue membership (the depth-peel pass) stays an
	// explicit instance decision - SetTransparent - because not every shader implements the
	// peel test.
	m_blendMode = p_blendMode;
	m_blendable = p_blendMode != Rendering::Settings::EBlendMode::BLEND_OPAQUE;
}


void Rendering::Data::MaterialInstance::SetUserInterface(bool p_userInterface)
{
	m_userInterface = p_userInterface;
}


void Rendering::Data::MaterialInstance::SetBackfaceCulling(bool p_backfaceCulling)
{
	m_backfaceCulling = p_backfaceCulling;
}


void Rendering::Data::MaterialInstance::SetPolygonOffsetFill(bool p_polygonOffsetFill)
{
	m_polygonOffsetFill = p_polygonOffsetFill;
}


void Rendering::Data::MaterialInstance::SetFrontfaceCulling(bool p_frontfaceCulling)
{
	m_frontfaceCulling = p_frontfaceCulling;
}


void Rendering::Data::MaterialInstance::SetDepthTest(bool p_depthTest)
{
	m_depthTest = p_depthTest;
}


void Rendering::Data::MaterialInstance::SetDepthWriting(bool p_depthWriting)
{
	m_depthWriting = p_depthWriting;
}


void Rendering::Data::MaterialInstance::SetColorWriting(bool p_colorWriting)
{
	m_colorWriting = p_colorWriting;
}


void Rendering::Data::MaterialInstance::SetCastShadows(bool p_castShadows)
{
	m_castShadows = p_castShadows;
}


void Rendering::Data::MaterialInstance::SetReceiveShadows(bool p_receiveShadows)
{
	m_receiveShadows = p_receiveShadows;
}


void Rendering::Data::MaterialInstance::SetCapturedByReflectionProbes(bool p_capturedByReflectionProbes)
{
	m_capturedByReflectionProbes = p_capturedByReflectionProbes;
}


void Rendering::Data::MaterialInstance::SetReceiveReflections(bool p_receiveReflections)
{
	m_receiveReflections = p_receiveReflections;
}


void Rendering::Data::MaterialInstance::SetGPUInstances(int p_instances)
{
	m_gpuInstances = p_instances;
}


bool Rendering::Data::MaterialInstance::IsBlendable() const
{
	return m_blendable ? *m_blendable : (m_definition ? m_definition->IsBlendable() : false);
}


bool Rendering::Data::MaterialInstance::IsTransparent() const
{
	return m_transparent ? *m_transparent : (m_definition ? m_definition->IsTransparent() : false);
}


bool Rendering::Data::MaterialInstance::IsUserInterface() const
{
	return m_userInterface ? *m_userInterface : (m_definition ? m_definition->IsUserInterface() : false);
}


bool Rendering::Data::MaterialInstance::HasBackfaceCulling() const
{
	return m_backfaceCulling ? *m_backfaceCulling : (m_definition ? m_definition->HasBackfaceCulling() : true);
}


bool Rendering::Data::MaterialInstance::HasFrontfaceCulling() const
{
	return m_frontfaceCulling ? *m_frontfaceCulling : (m_definition ? m_definition->HasFrontfaceCulling() : false);
}


bool Rendering::Data::MaterialInstance::HasDepthTest() const
{
	return m_depthTest ? *m_depthTest : (m_definition ? m_definition->HasDepthTest() : true);
}


bool Rendering::Data::MaterialInstance::HasDepthWriting() const
{
	return m_depthWriting ? *m_depthWriting : (m_definition ? m_definition->HasDepthWriting() : true);
}


bool Rendering::Data::MaterialInstance::HasColorWriting() const
{
	return m_colorWriting ? *m_colorWriting : (m_definition ? m_definition->HasColorWriting() : true);
}


bool Rendering::Data::MaterialInstance::IsShadowCaster() const
{
	return m_castShadows ? *m_castShadows : (m_definition ? m_definition->IsShadowCaster() : true);
}


bool Rendering::Data::MaterialInstance::IsShadowReceiver() const
{
	return m_receiveShadows ? *m_receiveShadows : (m_definition ? m_definition->IsShadowReceiver() : true);
}


bool Rendering::Data::MaterialInstance::IsCapturedByReflectionProbes() const
{
	return m_capturedByReflectionProbes ? *m_capturedByReflectionProbes : (m_definition ? m_definition->IsCapturedByReflectionProbes() : true);
}


bool Rendering::Data::MaterialInstance::IsReflectionReceiver() const
{
	return m_receiveReflections ? *m_receiveReflections : (m_definition ? m_definition->IsReflectionReceiver() : true);
}


int Rendering::Data::MaterialInstance::GetDrawOrder() const
{
	return m_drawOrder ? *m_drawOrder : (m_definition ? m_definition->GetDrawOrder() : 1000);
}


int Rendering::Data::MaterialInstance::GetGPUInstances() const
{
	return m_gpuInstances ? *m_gpuInstances : (m_definition ? m_definition->GetGPUInstances() : 1);
}


float Rendering::Data::MaterialInstance::GetLineWidth() const
{
	return m_lineWidth ? *m_lineWidth : (m_definition ? m_definition->GetLineWidth() : 1.0f);
}


Rendering::Settings::EBlendMode Rendering::Data::MaterialInstance::GetBlendMode() const
{
	if (m_blendMode) { return *m_blendMode; }
	return m_definition ? m_definition->GetBlendMode() : Rendering::Settings::EBlendMode::BLEND_OPAQUE;
}


bool Rendering::Data::MaterialInstance::SupportsOrthographic() const
{
	return m_supportOrthographic ? *m_supportOrthographic : (m_definition ? m_definition->SupportsOrthographic() : true);
}


bool Rendering::Data::MaterialInstance::SupportsPerspective() const
{
	return m_supportPerspective ? *m_supportPerspective : (m_definition ? m_definition->SupportsPerspective() : true);
}

bool Rendering::Data::MaterialInstance::HasPolygonOffsetFill() const
{
	return m_polygonOffsetFill ? *m_polygonOffsetFill : (m_definition ? m_definition->HasPolygonOffsetFill() : false);
}


const Rendering::Data::StateMask Rendering::Data::MaterialInstance::GenerateStateMask() const
{
	StateMask stateMask;
	stateMask.depthWriting = HasDepthWriting();
	stateMask.colorWriting = HasColorWriting();
	stateMask.blendable = IsBlendable();
	stateMask.depthTest = HasDepthTest();
	stateMask.frontfaceCulling = HasFrontfaceCulling();
	stateMask.backfaceCulling = HasBackfaceCulling();
	stateMask.polygonOffsetFill = HasPolygonOffsetFill();
	stateMask.blendMode = GetBlendMode();
	stateMask.lineWidth = GetLineWidth();
	return stateMask;
}


void Rendering::Data::MaterialInstance::SetFeatures(const Data::FeatureSet& p_features)
{
	m_features = p_features;
}


void Rendering::Data::MaterialInstance::AddFeature(const std::string& p_feature)
{
	m_features.insert(p_feature);
}


void Rendering::Data::MaterialInstance::RemoveFeature(const std::string& p_feature)
{
	m_features.erase(p_feature);
}

void Rendering::Data::MaterialInstance::EnableFeature(const std::string& p_feature,bool flag)
{
	if (flag) {
		m_features.insert(p_feature);
	}
	else
	{
		m_features.erase(p_feature);
	}
}


bool Rendering::Data::MaterialInstance::HasFeature(const std::string& p_feature) const
{
	return m_features.contains(p_feature);
}


bool Rendering::Data::MaterialInstance::SupportsFeature(const std::string& p_feature) const
{
	return ShaderOf()->GetFeatures().contains(p_feature);
}


bool Rendering::Data::MaterialInstance::HasPass(const std::string& p_pass) const
{
	return ShaderOf()->GetPasses().contains(p_pass);
}


bool Rendering::Data::MaterialInstance::SupportsProjectionMode(Rendering::Settings::EProjectionMode p_projectionMode) const
{
	using enum Rendering::Settings::EProjectionMode;
	switch (p_projectionMode)
	{
	case ORTHOGRAPHIC: return SupportsOrthographic();
	case PERSPECTIVE: return SupportsPerspective();
	}
	return true;
}


void Rendering::Data::MaterialInstance::SetLineWidth(float p_width)
{
	m_lineWidth = p_width;
}


void Rendering::Data::MaterialInstance::SetTransparent(bool transparent)
{
	m_transparent = transparent;
	if (transparent) {
		m_blendable = true;
	}
}


void Rendering::Data::MaterialInstance::InvalidatePropertySignature()
{
	++m_stablePropertySignatureVersion;
	++m_singleUsePropertySignatureVersion;
}


Rendering::Data::MaterialSignatureSet Rendering::Data::MaterialInstance::CalculateSignature(Rendering::HAL::ShaderProgram& p_selectedProgram, Rendering::HAL::Texture* p_emptyTexture2D, Rendering::HAL::Texture* p_emptyTextureCube)
{
	ZoneScoped;

	MaterialSignatureSet signature;

	// Bind
	HashCombine(signature.bindSignature, this);
	HashCombine(signature.bindSignature, &p_selectedProgram);

	// Stable Properties
	signature.stablePropertySignature = signature.bindSignature;
	HashCombine(signature.stablePropertySignature, m_stablePropertySignatureVersion);
	// ...and the definition's revision: an instance answers every property it does not override
	// from its definition (the .mat asset the designer edits), so an edit there has to invalidate
	// the upload. It did not, and the revision check in UploadProperties sat behind this very
	// signature - a texture assigned in the material editor therefore never reached an instance
	// that already existed (the preview, or a shape drawing with the asset), and the map looked
	// like it did nothing until the material was reopened.
	if (m_definition != nullptr)
	{
		HashCombine(signature.stablePropertySignature, m_definition->Revision());
	}
	HashCombine(signature.stablePropertySignature, p_emptyTexture2D);
	HashCombine(signature.stablePropertySignature, p_emptyTextureCube);

	// Single Use Properties
	signature.singleUsePropertySignature = signature.bindSignature;
	HashCombine(signature.singleUsePropertySignature, m_singleUsePropertySignatureVersion);
	HashCombine(signature.singleUsePropertySignature, p_emptyTexture2D);
	HashCombine(signature.singleUsePropertySignature, p_emptyTextureCube);

	return signature;
}