#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <format>
#include <ranges>
#include <string_view>
#include <vector>
#include <tracy/Tracy.hpp>
#include <Rendering/Data/Material.h>
#include <Rendering/HAL/UniformBuffer.h>
#include <Rendering/HAL/TextureHandle.h>
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

	if (m_shader)
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

	if (m_shader != nullptr && m_shader->HasState())
	{
		const Rendering::Resources::Shader::State& state = m_shader->GetState();

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

Tools::Utils::OptRef<Rendering::HAL::ShaderProgram> Rendering::Data::Material::GetVariant(
	std::optional<const std::string_view> p_pass,
	Tools::Utils::OptRef<const Data::FeatureSet> p_override
) const
{
	if (m_shader)
	{
		return m_shader->GetVariant(
			p_pass,
			p_override.value_or(m_features)
		);
	}

	return std::nullopt;
}

void Rendering::Data::Material::UploadProperties(bool uploadStableProperties, bool uploadSingleUseProperties, Rendering::HAL::Texture* p_emptyTexture2D, Rendering::HAL::Texture* p_emptyTextureCube, Rendering::HAL::Texture* p_brdfLut)
{
	ZoneScoped;

	// Packaged shaders carry their parameters as one std140 block (the per-material descriptor):
	// write the values by offset into it and bind it once, instead of resolving each name
	// against the program's reflected uniforms.
	const bool hasParameterBlock = m_shader != nullptr && m_shader->HasParameterBlock();
	if (hasParameterBlock)
	{
		UploadParameterBlock(uploadStableProperties, uploadSingleUseProperties, p_emptyTexture2D, p_emptyTextureCube, p_brdfLut);
	}

	// Properties the shader does not declare (an include's own uniforms, like PBR.glsl's
	// u_ReceiveShadows) are not in the block: they still go through the per-name path below,
	// while the declared ones were written by UploadParameterBlock.
	const auto isDeclaredParameter = [this](const std::string& p_name) {
		for (const Rendering::Resources::Shader::Parameter& parameter : m_shader->GetParameters())
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
			BindTexture(program, name, handle, uniformType == SAMPLER_2D ? p_emptyTexture2D : p_emptyTextureCube, textureSlot);
		}

		if (prop.singleUse)
		{
			value = UniformToPropertyValue(uniformData->defaultValue);
		}
	}
}

void Rendering::Data::Material::EnsureParameterSlice()
{
	const uint32_t blockSize = m_shader->GetParameterBlockSize();
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

void Rendering::Data::Material::ReleaseParameterSlice()
{
	if (m_parameterSlice.IsValid())
	{
		UniformBufferPool::Get().Release(m_parameterSlice);
		m_parameterSlice = {};
	}
}

void Rendering::Data::Material::BindParameterBlock()
{
	if (m_shader == nullptr || !m_shader->HasParameterBlock())
	{
		return;
	}

	EnsureParameterSlice();
	UniformBufferPool::Get().Bind(m_parameterSlice, kMaterialParameterBinding);
}

void Rendering::Data::Material::WriteBlockValue(const std::string& p_type, const MaterialPropertyType& p_value, uint32_t p_offset)
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

void Rendering::Data::Material::UploadParameterBlock(
	bool p_uploadStableProperties,
	bool p_uploadSingleUseProperties,
	Rendering::HAL::Texture* p_emptyTexture2D,
	Rendering::HAL::Texture* p_emptyTextureCube,
	Rendering::HAL::Texture* p_brdfLut)
{
	EnsureParameterSlice();
	if (!m_parameterSlice.IsValid())
	{
		return;
	}

	const uint32_t blockSize = m_shader->GetParameterBlockSize();
	m_parameterData.assign(blockSize, 0);

	for (const Rendering::Resources::Shader::Parameter& parameter : m_shader->GetParameters())
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
	for (const Rendering::Resources::Shader::Parameter& parameter : m_shader->GetParameters())
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
		if (auto* target = handle != nullptr ? handle : fallback)
		{
			target->Bind(parameter.binding);
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

		const auto parameter = std::ranges::find_if(m_shader->GetParameters(),
			[&name](const Rendering::Resources::Shader::Parameter& p_parameter) {
				return p_parameter.name == name
					|| name.starts_with(p_parameter.name + "[");
			});
		if (parameter != m_shader->GetParameters().end())
		{
			property.value = ParameterDefaultValue(*parameter);
		}
		else
		{
			property.value = ZeroOf(property.value);
		}
	}
}

void Rendering::Data::Material::BindGlobalTexture(const std::string& p_name, Rendering::HAL::Texture* p_texture)
{
	if (m_shader == nullptr || p_texture == nullptr)
	{
		return;
	}

	for (const Rendering::Resources::Shader::Parameter& parameter : m_shader->GetParameters())
	{
		if (parameter.isSampler && parameter.name == p_name)
		{
			p_texture->Bind(parameter.binding);
			return;
		}
	}
}

bool Rendering::Data::Material::ResetPropertyToDefault(const std::string& p_name)
{
	if (m_shader == nullptr || !m_shader->HasParameterTable())
	{
		return false;
	}

	for (const Rendering::Resources::Shader::Parameter& parameter : m_shader->GetParameters())
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

void Rendering::Data::Material::UpdateProperties()
{
	InvalidatePropertySignature();

	// A shader that came out of a package knows its parameters: name, GLSL type, texture slot or
	// not, array size, and the initialiser the material declared. Building the property set from
	// that list keeps it independent of which uniforms a driver happened to keep active in a
	// given variant - and it is the half of the package's reflection the runtime was not reading
	// yet (see Shader::HasParameterTable).
	if (m_shader != nullptr && m_shader->HasParameterTable())
	{
		for (const Rendering::Resources::Shader::Parameter& parameter : m_shader->GetParameters())
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
	auto variants_view = m_shader->GetVariants()
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
Rendering::Data::MaterialSignatureSet Rendering::Data::Material::Bind(
	HAL::Texture* p_emptyTexture,
	HAL::Texture* p_emptyTextureCube,
	std::optional<const std::string_view> p_pass,
	Tools::Utils::OptRef<const Data::FeatureSet> p_featureSetOverride,
	std::optional<MaterialSignatureSet> p_previousMaterialSignature 
)
{
	ZoneScoped;
	auto& program = m_shader->GetVariant(
		p_pass,
		p_featureSetOverride.value_or(m_features)
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

void Rendering::Data::Material::Unbind(bool p_resetBoundProgram)
{
	if (p_resetBoundProgram)
	{
		m_programInUse->Unbind();
	}

	m_programInUse.reset();
}

bool Rendering::Data::Material::HasProperty(const std::string& p_name) const
{
	return m_properties.contains(p_name);
}

void Rendering::Data::Material::SetProperty(const std::string p_name, const MaterialPropertyType& p_value, bool p_singleUse)
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
			lateLoadTextures[p_name] = p_path;
		}
	}
}

bool Rendering::Data::Material::TrySetProperty(const std::string& p_name, const MaterialPropertyType& p_value, bool p_singleUse)
{
	if (HasProperty(p_name))
	{
		SetProperty(p_name, p_value, p_singleUse);
		return true;
	}
	return false;
}

Tools::Utils::OptRef<const Rendering::Data::MaterialProperty> Rendering::Data::Material::GetProperty(const std::string p_key) const
{
	if (m_properties.find(p_key) != m_properties.end())
	{
		return m_properties.at(p_key);
	}
	return std::nullopt;
}

Rendering::Resources::Shader*& Rendering::Data::Material::GetShader()
{
	return m_shader;
}

bool Rendering::Data::Material::HasShader() const
{
	return m_shader;
}

bool Rendering::Data::Material::IsValid() const
{
	return HasShader();
}

void Rendering::Data::Material::SetOrthographicSupport(bool p_supportOrthographic)
{
	m_supportOrthographic = p_supportOrthographic;
}

void Rendering::Data::Material::SetPerspectiveSupport(bool p_supportPerspective)
{
	m_supportPerspective = p_supportPerspective;
}

void Rendering::Data::Material::SetDrawOrder(int p_order)
{
	m_drawOrder = p_order;
}

void Rendering::Data::Material::SetBlendable(bool p_transparent)
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

Rendering::Settings::EBlendMode Rendering::Data::Material::GetBlendMode() const
{
	return m_blendMode;
}

void Rendering::Data::Material::SetBlendMode(Rendering::Settings::EBlendMode p_blendMode)
{
	// The blend mode is about factors only. Queue membership (the depth-peel pass) stays an
	// explicit instance decision - SetTransparent - because not every shader implements the
	// peel test.
	m_blendMode = p_blendMode;
	m_blendable = p_blendMode != Rendering::Settings::EBlendMode::BLEND_OPAQUE;
}

void Rendering::Data::Material::SetUserInterface(bool p_userInterface)
{
	m_userInterface = p_userInterface;
}

void Rendering::Data::Material::SetBackfaceCulling(bool p_backfaceCulling)
{
	m_backfaceCulling = p_backfaceCulling;
}

void Rendering::Data::Material::SetPolygonOffsetFill(bool p_polygonOffsetFill)
{
	m_polygonOffsetFill = p_polygonOffsetFill;
}

void Rendering::Data::Material::SetFrontfaceCulling(bool p_frontfaceCulling)
{
	m_frontfaceCulling = p_frontfaceCulling;
}

void Rendering::Data::Material::SetDepthTest(bool p_depthTest)
{
	m_depthTest = p_depthTest;
}

void Rendering::Data::Material::SetDepthWriting(bool p_depthWriting)
{
	m_depthWriting = p_depthWriting;
}

void Rendering::Data::Material::SetColorWriting(bool p_colorWriting)
{
	m_colorWriting = p_colorWriting;
}

void Rendering::Data::Material::SetCastShadows(bool p_castShadows)
{
	m_castShadows = p_castShadows;
}

void Rendering::Data::Material::SetReceiveShadows(bool p_receiveShadows)
{
	m_receiveShadows = p_receiveShadows;
}

void Rendering::Data::Material::SetCapturedByReflectionProbes(bool p_capturedByReflectionProbes)
{
	m_capturedByReflectionProbes = p_capturedByReflectionProbes;
}

void Rendering::Data::Material::SetReceiveReflections(bool p_receiveReflections)
{
	m_receiveReflections = p_receiveReflections;
}

void Rendering::Data::Material::SetGPUInstances(int p_instances)
{
	m_gpuInstances = p_instances;
}

int Rendering::Data::Material::GetDrawOrder() const
{
	return m_drawOrder;
}

bool Rendering::Data::Material::IsBlendable() const
{
	return m_blendable;
}

bool Rendering::Data::Material::IsTransparent() const
{
	return m_transparent;
}

bool Rendering::Data::Material::IsUserInterface() const
{
	return m_userInterface;
}

bool Rendering::Data::Material::HasBackfaceCulling() const
{
	return m_backfaceCulling;
}

bool Rendering::Data::Material::HasFrontfaceCulling() const
{
	return m_frontfaceCulling;
}

bool Rendering::Data::Material::HasDepthTest() const
{
	return m_depthTest;
}

bool Rendering::Data::Material::HasDepthWriting() const
{
	return m_depthWriting;
}

bool Rendering::Data::Material::HasColorWriting() const
{
	return m_colorWriting;
}

bool Rendering::Data::Material::IsShadowCaster() const
{
	return m_castShadows;
}

bool Rendering::Data::Material::IsShadowReceiver() const
{
	return m_receiveShadows;
}

bool Rendering::Data::Material::IsCapturedByReflectionProbes() const
{
	return m_capturedByReflectionProbes;
}

bool Rendering::Data::Material::IsReflectionReceiver() const
{
	return m_receiveReflections;
}

int Rendering::Data::Material::GetGPUInstances() const
{
	return m_gpuInstances;
}

const Rendering::Data::StateMask Rendering::Data::Material::GenerateStateMask() const
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
	stateMask.lineWidth = lineWitdh;
	return stateMask;
}

Rendering::Data::Material::PropertyMap& Rendering::Data::Material::GetProperties()
{
	return m_properties;
}

Rendering::Data::FeatureSet& Rendering::Data::Material::GetFeatures()
{
	return m_features;
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
	return m_shader->GetFeatures().contains(p_feature);
}

bool Rendering::Data::Material::HasPass(const std::string& p_pass) const
{
	return m_shader->GetPasses().contains(p_pass);
}

bool Rendering::Data::Material::SupportsOrthographic() const
{
	return m_supportOrthographic;
}

bool Rendering::Data::Material::SupportsPerspective() const
{
	return m_supportPerspective;
}

bool Rendering::Data::Material::SupportsProjectionMode(Rendering::Settings::EProjectionMode p_projectionMode) const
{
	using enum Rendering::Settings::EProjectionMode;
	switch (p_projectionMode)
	{
	case ORTHOGRAPHIC: return SupportsOrthographic();
	case PERSPECTIVE: return SupportsPerspective();
	}
	return true;
}

void Rendering::Data::Material::SetLineWidth(float p_width)
{
	lineWitdh = p_width;
}

void Rendering::Data::Material::SetTransparent(bool transparent)
{
	m_transparent = transparent;
	if (transparent) {
		m_blendable = true;
	}
}

void Rendering::Data::Material::InvalidatePropertySignature()
{
	++m_stablePropertySignatureVersion;
	++m_singleUsePropertySignatureVersion;
}

Rendering::Data::MaterialSignatureSet Rendering::Data::Material::CalculateSignature(Rendering::HAL::ShaderProgram& p_selectedProgram, Rendering::HAL::Texture* p_emptyTexture2D, Rendering::HAL::Texture* p_emptyTextureCube)
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
