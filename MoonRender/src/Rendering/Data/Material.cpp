#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <format>
#include <iostream>
#include <ranges>
#include <set>
#include <string_view>
#include <vector>
#include <tracy/Tracy.hpp>
#include <Rendering/Data/Material.h>
#include <Rendering/HAL/UniformBuffer.h>
#include <Rendering/HAL/TextureHandle.h>
#include <Rendering/Resources/ShaderPackage.h>
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
	Rendering::Data::MaterialPropertyType ParameterDefaultValue(
		const Rendering::Resources::Shader::Parameter& p_parameter)
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
}

// ---- MaterialInstance: the override-only half of the pair -------------------------------------

Rendering::Data::MaterialInstance::MaterialInstance(Material& p_definition)
	: m_definition(&p_definition)
{
	// An instance answers every question the definition does, with its own values on top. The
	// property map is seeded from the definition so the upload loops (which iterate this map) keep
	// working unchanged; a later change on the definition does not reach an existing instance.
	m_properties = p_definition.Properties();
	CopyStateFrom(p_definition);
}

Rendering::Data::MaterialInstance::~MaterialInstance()
{
	ReleaseParameterSlice();
}

const Rendering::Data::MaterialProperty* Rendering::Data::MaterialInstance::FindProperty(const std::string& p_name) const
{
	const auto it = m_properties.find(p_name);
	return it == m_properties.end() ? nullptr : &it->second;
}

Rendering::Resources::Shader* Rendering::Data::MaterialInstance::ShaderOf() const
{
	return m_definition != nullptr ? m_definition->GetShader() : nullptr;
}

const Rendering::Data::FeatureSet& Rendering::Data::MaterialInstance::FeaturesOf() const
{
	static const Data::FeatureSet empty;
	return m_definition != nullptr ? m_definition->GetFeatures() : empty;
}

void Rendering::Data::MaterialInstance::CopyStateFrom(const MaterialInstance& p_source)
{
	m_supportOrthographic = p_source.m_supportOrthographic;
	m_supportPerspective = p_source.m_supportPerspective;
	m_userInterface = p_source.m_userInterface;
	m_blendable = p_source.m_blendable;
	m_blendMode = p_source.m_blendMode;
	m_transparent = p_source.m_transparent;
	m_backfaceCulling = p_source.m_backfaceCulling;
	m_frontfaceCulling = p_source.m_frontfaceCulling;
	m_polygonOffsetFill = p_source.m_polygonOffsetFill;
	m_depthTest = p_source.m_depthTest;
	m_depthWriting = p_source.m_depthWriting;
	m_colorWriting = p_source.m_colorWriting;
	m_castShadows = p_source.m_castShadows;
	m_receiveShadows = p_source.m_receiveShadows;
	m_capturedByReflectionProbes = p_source.m_capturedByReflectionProbes;
	m_receiveReflections = p_source.m_receiveReflections;
	m_gpuInstances = p_source.m_gpuInstances;
	m_drawOrder = p_source.m_drawOrder;
	m_lineWidth = p_source.m_lineWidth;
}

std::unique_ptr<Rendering::Data::MaterialInstance> Rendering::Data::Material::CreateInstance()
{
	return std::make_unique<MaterialInstance>(*this);
}
Rendering::Data::Material::Material(Rendering::Resources::Shader* p_shader)
{
	SetShader(p_shader);
}

Rendering::Data::Material::~Material()
{
	// The pool keeps the byte range; it only lives as long as the material does.
	ReleaseParameterSlice();
}

void Rendering::Data::Material::SetShader(Rendering::Resources::Shader* p_shader)
{
	m_shader = p_shader;

	// The block layout (or the presence of a block) belongs to the shader.
	ReleaseParameterSlice();

	// Rebinding to a shader re-creates the instance's state from the definition: the package's
	// material block is the authority for blending / culling / depth writing, and whatever the
	// call site (or a .mat's settings) sets afterwards is the override.
	ResetStateToShader();

	if (m_shader != nullptr)
	{
		m_properties.clear();
		UpdateProperties();
	}
	else
	{
		m_properties.clear();
		InvalidatePropertySignature();
	}
}

void Rendering::Data::Material::ResetStateToShader()
{
	// Engine defaults first; the material block then says what this material is.
	m_blendable = false;
	m_blendMode = Rendering::Settings::EBlendMode::BLEND_OPAQUE;
	m_backfaceCulling = true;
	m_frontfaceCulling = false;
	m_depthWriting = true;

	if (ShaderOf() != nullptr && ShaderOf()->HasState())
	{
		const Rendering::Resources::Shader::State& state = ShaderOf()->GetState();

		if (state.blendable.has_value()) { m_blendable = state.blendable.value(); }
		if (state.blendMode.has_value())
		{
			m_blendMode = state.blendMode.value();
			// The definition decides how the colour is blended. Whether the material goes
			// through the transparent (depth-peel) queue stays an instance decision: a
			// transparent-by-DSL shader that does not implement the peel test (the grid, the
			// gizmos) must not be peeled just because of its default.
			m_blendable = m_blendMode != Rendering::Settings::EBlendMode::BLEND_OPAQUE;
		}
		if (state.backfaceCulling.has_value()) { m_backfaceCulling = state.backfaceCulling.value(); }
		if (state.frontfaceCulling.has_value()) { m_frontfaceCulling = state.frontfaceCulling.value(); }
		if (state.depthWriting.has_value()) { m_depthWriting = state.depthWriting.value(); }
	}
}

Tools::Utils::OptRef<Rendering::HAL::ShaderProgram> Rendering::Data::MaterialInstance::GetVariant(
	std::optional<const std::string_view> p_pass,
	Tools::Utils::OptRef<const Data::FeatureSet> p_override
) const
{
	if (ShaderOf() != nullptr)
	{
		return ShaderOf()->GetVariant(
			p_pass,
			p_override.value_or(FeaturesOf())
		);
	}

	return std::nullopt;
}

void Rendering::Data::MaterialInstance::UploadProperties(bool uploadStableProperties, bool uploadSingleUseProperties, Rendering::HAL::Texture* p_emptyTexture2D, Rendering::HAL::Texture* p_emptyTextureCube, Rendering::HAL::Texture* p_brdfLut, Rendering::HAL::Texture* p_flatNormalTexture)
{
	ZoneScoped;

	// Packaged shaders carry their parameters as one std140 block (the per-material descriptor):
	// write the values by offset into it and bind it once, instead of resolving each name
	// against the program's reflected uniforms.
	const bool hasParameterBlock = ShaderOf() != nullptr && ShaderOf()->HasParameterBlock();
	if (hasParameterBlock)
	{
		UploadParameterBlock(uploadStableProperties, uploadSingleUseProperties, p_emptyTexture2D, p_emptyTextureCube, p_brdfLut, p_flatNormalTexture);
	}

	/** The 1x1 texture a sampler gets when the material instance has none: the material's own
	 * `fallback :` kind when the package declared one, the engine's defaults otherwise. */
	const auto fallbackTexture = [this, p_emptyTexture2D, p_emptyTextureCube, p_flatNormalTexture](
		const std::string& p_name, bool p_isCube) -> Rendering::HAL::Texture*
	{
		for (const Rendering::Resources::Shader::Parameter& parameter : ShaderOf()->GetParameters())
		{
			if (parameter.isSampler && parameter.name == p_name
				&& parameter.fallback == static_cast<uint8_t>(Rendering::Resources::ShaderPackage::SamplerFallback::FlatNormal)
				&& p_flatNormalTexture != nullptr)
			{
				return p_flatNormalTexture;
			}
		}
		return p_isCube ? p_emptyTextureCube : p_emptyTexture2D;
	};

	// Properties the shader does not declare (an include's own uniforms, like PBR.glsl's
	// u_ReceiveShadows) are not in the block: they still go through the per-name path below,
	// while the declared ones were written by UploadParameterBlock.
	const auto isDeclaredParameter = [this](const std::string& p_name) {
		for (const Rendering::Resources::Shader::Parameter& parameter : ShaderOf()->GetParameters())
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
	int textureSlot = 0;
	for (auto& [name, prop] : m_properties)
	{
		if (!uploadStableProperties && !prop.singleUse) continue;
		if (!uploadSingleUseProperties && prop.singleUse) continue;
		if (hasParameterBlock && isDeclaredParameter(name)) continue;

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
		}
	}
}

void Rendering::Data::MaterialInstance::EnsureParameterSlice()
{
	const uint32_t blockSize = ShaderOf()->GetParameterBlockSize();
	if (blockSize == 0)
	{
		ReleaseParameterSlice();
		return;
	}

	if (m_parameterSlice.size == blockSize)
	{
		return;
	}

	// A hot reloaded shader can change the block layout; a destroyed one leaves nothing to bind.
	ReleaseParameterSlice();
	m_parameterSlice = UniformBufferPool::Get().Acquire(blockSize);
}

void Rendering::Data::MaterialInstance::ReleaseParameterSlice()
{
	if (m_parameterSlice.IsValid())
	{
		UniformBufferPool::Get().Release(m_parameterSlice);
		m_parameterSlice = {};
	}
}

void Rendering::Data::MaterialInstance::BindParameterBlock()
{
	if (ShaderOf() == nullptr || !ShaderOf()->HasParameterBlock())
	{
		return;
	}

	EnsureParameterSlice();
	UniformBufferPool::Get().Bind(m_parameterSlice, kMaterialParameterBinding);
}

void Rendering::Data::MaterialInstance::WriteBlockValue(const std::string& p_type, const MaterialPropertyType& p_value, uint32_t p_offset)
{
	using namespace Maths;

	if (m_parameterData.size() < p_offset)
	{
		return;
	}

	auto* data = m_parameterData.data() + p_offset;

	if (p_type == "bool")
	{
		if (const auto* value = std::get_if<bool>(&p_value))
		{
			const int32_t stored = *value ? 1 : 0;
			std::memcpy(data, &stored, sizeof(stored));
		}
	}
	else if (p_type == "int")
	{
		if (const auto* value = std::get_if<int>(&p_value))
		{
			std::memcpy(data, value, sizeof(*value));
		}
	}
	else if (p_type == "float")
	{
		if (const auto* value = std::get_if<float>(&p_value))
		{
			std::memcpy(data, value, sizeof(*value));
		}
	}
	else if (p_type == "vec2")
	{
		if (const auto* value = std::get_if<FVector2>(&p_value))
		{
			std::memcpy(data, value, sizeof(float) * 2);
		}
	}
	else if (p_type == "vec3")
	{
		if (const auto* value = std::get_if<FVector3>(&p_value))
		{
			std::memcpy(data, value, sizeof(float) * 3);
		}
	}
	else if (p_type == "vec4")
	{
		if (const auto* value = std::get_if<FVector4>(&p_value))
		{
			std::memcpy(data, value, sizeof(float) * 4);
		}
	}
	else if (p_type == "mat3")
	{
		if (const auto* value = std::get_if<FMatrix3>(&p_value))
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
		}
	}
	else if (p_type == "mat4")
	{
		if (const auto* value = std::get_if<FMatrix4>(&p_value))
		{
			for (uint32_t column = 0; column < 4; ++column)
			{
				for (uint32_t row = 0; row < 4; ++row)
				{
					const float element = value->data[row * 4 + column];
					std::memcpy(data + column * 16 + row * 4, &element, sizeof(element));
				}
			}
		}
	}
}

void Rendering::Data::MaterialInstance::UploadParameterBlock(
	bool p_uploadStableProperties,
	bool p_uploadSingleUseProperties,
	Rendering::HAL::Texture* p_emptyTexture2D,
	Rendering::HAL::Texture* p_emptyTextureCube,
	Rendering::HAL::Texture* p_brdfLut,
	Rendering::HAL::Texture* p_flatNormalTexture)
{
	EnsureParameterSlice();
	if (!m_parameterSlice.IsValid())
	{
		return;
	}

	const uint32_t blockSize = ShaderOf()->GetParameterBlockSize();
	m_parameterData.assign(blockSize, 0);

	for (const Rendering::Resources::Shader::Parameter& parameter : ShaderOf()->GetParameters())
	{
		if (parameter.isSampler || parameter.offset < 0)
		{
			continue;
		}

		if (parameter.count > 0)
		{
			// Arrays are addressed one element at a time ("samples[17]"), exactly how the
			// property set stores them.
			for (uint32_t i = 0; i < parameter.count; ++i)
			{
				const auto property = GetProperty(std::format("{}[{}]", parameter.name, i));
				if (property)
				{
					WriteBlockValue(parameter.type, property->value,
						static_cast<uint32_t>(parameter.offset) + parameter.stride * i);
				}
			}
		}
		else if (const auto property = GetProperty(parameter.name))
		{
			WriteBlockValue(parameter.type, property->value, static_cast<uint32_t>(parameter.offset));
		}
	}

	UniformBufferPool::Get().Write(m_parameterSlice, m_parameterData.data());
	UniformBufferPool::Get().Bind(m_parameterSlice, kMaterialParameterBinding);

	// Samplers: one fixed texture unit per declaration, set by the GLSL's layout(binding = N).
	for (const Rendering::Resources::Shader::Parameter& parameter : ShaderOf()->GetParameters())
	{
		if (!parameter.isSampler)
		{
			continue;
		}

		Rendering::HAL::TextureHandle* handle = nullptr;
		if (const auto property = GetProperty(parameter.name))
		{
			if (auto textureHandle = std::get_if<Rendering::HAL::TextureHandle*>(&property->value))
			{
				handle = *textureHandle;
			}
			else if (auto texture = std::get_if<Rendering::Resources::Texture*>(&property->value);
				texture != nullptr && *texture != nullptr)
			{
				handle = &(*texture)->GetTexture();
			}
		}

		// The split-sum DFG LUT is global to the view, like Filament's frame uniform. Binding it
		// through the material property was fragile: an instance without the parameter (the
		// material preview) sampled the white empty 2D, giving DFG=(1,1), E~=1 and no diffuse.
		if (parameter.name == "_BRDFLut" && p_brdfLut != nullptr)
		{
			p_brdfLut->Bind(parameter.binding);
			continue;
		}

		Rendering::HAL::TextureHandle* fallback =
			parameter.type == "samplerCube" ? p_emptyTextureCube : p_emptyTexture2D;
		const char* fallbackName = parameter.type == "samplerCube" ? "empty cube (black)" : "empty 2D (white)";
		if (parameter.fallback == static_cast<uint8_t>(Rendering::Resources::ShaderPackage::SamplerFallback::FlatNormal)
			&& p_flatNormalTexture != nullptr)
		{
			fallback = p_flatNormalTexture;
			fallbackName = "flat normal (0.5,0.5,1)";
		}
		if (auto* target = handle != nullptr ? handle : fallback)
		{
			target->Bind(parameter.binding);
		}

		// MOON_DEBUG_SAMPLERS: what each sampler slot actually got, once per shader and slot.
		// The "why is my texture ignored" question has two very different answers (the material
		// property is empty vs. it is set but the shader never reads it), and this says which.
		if (std::getenv("MOON_DEBUG_SAMPLERS") != nullptr)
		{
			static std::set<std::string> reported;
			const std::string key = (ShaderOf() != nullptr ? ShaderOf()->path : std::string("?")) + "|" + parameter.name;
			if (reported.insert(key).second)
			{
				std::cout << "[Sampler] " << (ShaderOf() != nullptr ? ShaderOf()->path : "?") << " "
					<< parameter.name << " unit " << parameter.binding << " <- "
					<< (handle != nullptr ? "material texture" : fallbackName) << std::endl;
			}
		}
	}

	// Single-use properties are per-frame scratch: put them back to their defaults, like the
	// loose path did, so a value set for one draw never sticks to the material.
	for (auto& [name, property] : m_properties)
	{
		if (!property.singleUse)
		{
			continue;
		}

		const auto parameter = std::ranges::find_if(ShaderOf()->GetParameters(),
			[&name](const Rendering::Resources::Shader::Parameter& p_parameter) {
				return p_parameter.name == name
					|| name.starts_with(p_parameter.name + "[");
			});
		if (parameter != ShaderOf()->GetParameters().end())
		{
			property.value = ParameterDefaultValue(*parameter);
		}
		else
		{
			property.value = ZeroOf(property.value);
		}
	}
}

void Rendering::Data::MaterialInstance::BindGlobalTexture(const std::string& p_name, Rendering::HAL::Texture* p_texture)
{
	if (ShaderOf() == nullptr || p_texture == nullptr)
	{
		return;
	}

	for (const Rendering::Resources::Shader::Parameter& parameter : ShaderOf()->GetParameters())
	{
		if (parameter.isSampler && parameter.name == p_name)
		{
			p_texture->Bind(parameter.binding);
			// MOON_DEBUG_SAMPLERS also reports the per-draw bindings: the ones in UploadParameterBlock
			// happen once (and may have run before the sky pass baked the environment), while these
			// happen on every draw and are what the shader really samples.
			if (std::getenv("MOON_DEBUG_SAMPLERS") != nullptr)
			{
				static std::set<std::string> reported;
				const std::string key = ShaderOf()->path + "|draw|" + parameter.name;
				if (reported.insert(key).second)
				{
					std::cout << "[Sampler] " << ShaderOf()->path << " " << parameter.name << " unit "
						<< parameter.binding << " <- view texture (per draw)" << std::endl;
				}
			}
			return;
		}
	}
}

bool Rendering::Data::MaterialInstance::ResetPropertyToDefault(const std::string& p_name)
{
	if (ShaderOf() == nullptr || !ShaderOf()->HasParameterTable())
	{
		return false;
	}

	for (const Rendering::Resources::Shader::Parameter& parameter : ShaderOf()->GetParameters())
	{
		if (parameter.name != p_name)
		{
			continue;
		}
		if (!parameter.isUi)
		{
			return false;
		}

		const MaterialPropertyType value = ParameterDefaultValue(parameter);
		if (std::holds_alternative<std::monostate>(value))
		{
			return false;
		}

		if (HasProperty(p_name))
		{
			SetProperty(p_name, value);
		}
		else
		{
			m_properties.emplace(p_name, MaterialProperty{ .value = value, .singleUse = false });
			InvalidatePropertySignature();
		}
		return true;
	}
	return false;
}

void Rendering::Data::MaterialInstance::UpdateProperties()
{
	InvalidatePropertySignature();

	// A shader that came out of a package knows its parameters: name, GLSL type, texture slot or
	// not, array size, and the initialiser the material declared. Building the property set from
	// that list keeps it independent of which uniforms a driver happened to keep active in a
	// given variant - and it is the half of the package's reflection the runtime was not reading
	// yet (see Shader::HasParameterTable).
	if (ShaderOf() != nullptr && ShaderOf()->HasParameterTable())
	{
		for (const Rendering::Resources::Shader::Parameter& parameter : ShaderOf()->GetParameters())
		{
			const MaterialPropertyType value = ParameterDefaultValue(parameter);
			if (std::holds_alternative<std::monostate>(value))
			{
				continue;
			}

			if (parameter.count > 0)
			{
				// An array parameter is addressed one element at a time by the callers
				// ("samples[17]"): the elements are the properties.
				for (uint32_t i = 0; i < parameter.count; ++i)
				{
					m_properties.try_emplace(std::format("{}[{}]", parameter.name, i),
						MaterialProperty{ .value = value, .singleUse = false });
				}
			}
			else
			{
				m_properties.try_emplace(parameter.name,
					MaterialProperty{ .value = value, .singleUse = false });
			}
		}
		return;
	}

	// A shader built from source (the present pass, the blits) has no package behind it: the
	// driver's list is the only description of its uniforms.
	// Collect all uniform names currently used by the shader
	std::unordered_set<std::string> usedUniforms;
	auto variants_view = ShaderOf()->GetVariants()
		| std::views::values
		| std::views::join
		| std::views::values;

	for (const auto& variant : variants_view)
	{
		for (const auto& [name, uniformInfo] : variant->GetUniforms())
		{
			usedUniforms.insert(name);
			if (!m_properties.contains(name))
			{
				m_properties.emplace(name, MaterialProperty{
					.value = UniformToPropertyValue(uniformInfo.defaultValue),
					.singleUse = false
					});
			}
		}
	}
	std::erase_if(m_properties, [&usedUniforms](const auto& property) {
		return !usedUniforms.contains(property.first);
		});
}
// Note: this function is critical for performance, as it may be called many times during a frame.
// Avoid using any heavy operations or allocations inside this function.
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

	// The parameter block is per material, not per program: bind it on every draw so it is the
	// one bound even when another material uploaded its own buffer since the last draw.
	BindParameterBlock();

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
	return m_properties.contains(p_name);
}

void Rendering::Data::MaterialInstance::SetProperty(const std::string p_name, const MaterialPropertyType& p_value, bool p_singleUse)
{
	m_properties[p_name].value = p_value;
	m_properties[p_name].singleUse = p_singleUse;

	if (p_singleUse)
	{
		++m_singleUsePropertySignatureVersion;
	}
	else
	{
		++m_stablePropertySignatureVersion;
	}
}

void Rendering::Data::Material::LateUpdateTexture(const std::string& p_name, const std::string& p_path)
{
	if (HasProperty(p_name)) {
		auto& prop = m_properties[p_name];
		if (auto texture = std::holds_alternative<Rendering::Resources::Texture*>(prop.value))
		{
			m_lateLoadTextures[p_name] = p_path;
		}
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

Tools::Utils::OptRef<const Rendering::Data::MaterialProperty> Rendering::Data::MaterialInstance::GetProperty(const std::string p_key) const
{
	if (m_properties.find(p_key) != m_properties.end())
	{
		return m_properties.at(p_key);
	}
	return std::nullopt;
}

bool Rendering::Data::Material::HasShader() const
{
	return m_shader != nullptr;
}

bool Rendering::Data::MaterialInstance::HasPass(const std::string& p_pass) const
{
	const auto* shader = ShaderOf();
	return shader != nullptr && shader->GetPasses().contains(p_pass);
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
	if (p_transparent && m_blendMode == Rendering::Settings::EBlendMode::BLEND_OPAQUE)
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
	return m_blendable;
}

bool Rendering::Data::MaterialInstance::IsTransparent() const
{
	return m_transparent;
}

bool Rendering::Data::MaterialInstance::IsUserInterface() const
{
	return m_userInterface;
}

bool Rendering::Data::MaterialInstance::HasBackfaceCulling() const
{
	return m_backfaceCulling;
}

bool Rendering::Data::MaterialInstance::HasFrontfaceCulling() const
{
	return m_frontfaceCulling;
}

bool Rendering::Data::MaterialInstance::HasDepthTest() const
{
	return m_depthTest;
}

bool Rendering::Data::MaterialInstance::HasDepthWriting() const
{
	return m_depthWriting;
}

bool Rendering::Data::MaterialInstance::HasColorWriting() const
{
	return m_colorWriting;
}

bool Rendering::Data::MaterialInstance::IsShadowCaster() const
{
	return m_castShadows;
}

bool Rendering::Data::MaterialInstance::IsShadowReceiver() const
{
	return m_receiveShadows;
}

bool Rendering::Data::MaterialInstance::IsCapturedByReflectionProbes() const
{
	return m_capturedByReflectionProbes;
}

bool Rendering::Data::MaterialInstance::IsReflectionReceiver() const
{
	return m_receiveReflections;
}

const Rendering::Data::StateMask Rendering::Data::MaterialInstance::GenerateStateMask() const
{
	StateMask stateMask;
	stateMask.depthWriting = m_depthWriting;
	stateMask.colorWriting = m_colorWriting;
	stateMask.blendable = m_blendable;
	stateMask.depthTest = m_depthTest;
	stateMask.frontfaceCulling = m_frontfaceCulling;
	stateMask.backfaceCulling = m_backfaceCulling;
	stateMask.polygonOffsetFill = m_polygonOffsetFill;
	stateMask.blendMode = m_blendMode;
	stateMask.lineWidth = m_lineWidth;
	return stateMask;
}

void Rendering::Data::Material::SetFeatures(const Data::FeatureSet& p_features)
{
	m_features = p_features;
}

void Rendering::Data::Material::AddFeature(const std::string& p_feature)
{
	m_features.insert(p_feature);
}

void Rendering::Data::Material::RemoveFeature(const std::string& p_feature)
{
	m_features.erase(p_feature);
}
void Rendering::Data::Material::EnableFeature(const std::string& p_feature,bool flag)
{
	if (flag) {
		m_features.insert(p_feature);
	}
	else
	{
		m_features.erase(p_feature);
	}
}

bool Rendering::Data::Material::HasFeature(const std::string& p_feature) const
{
	return m_features.contains(p_feature);
}

bool Rendering::Data::Material::SupportsFeature(const std::string& p_feature) const
{
	return ShaderOf()->GetFeatures().contains(p_feature);
}

bool Rendering::Data::Material::HasPass(const std::string& p_pass) const
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
	HashCombine(signature.stablePropertySignature, p_emptyTexture2D);
	HashCombine(signature.stablePropertySignature, p_emptyTextureCube);

	// Single Use Properties
	signature.singleUsePropertySignature = signature.bindSignature;
	HashCombine(signature.singleUsePropertySignature, m_singleUsePropertySignatureVersion);
	HashCombine(signature.singleUsePropertySignature, p_emptyTexture2D);
	HashCombine(signature.singleUsePropertySignature, p_emptyTextureCube);

	return signature;
}