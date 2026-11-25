#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
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
#include <Rendering/Data/EngineDescriptorSets.h>
#include <Rendering/Data/MaterialBuilder.h>
#include <Rendering/Data/Texture.h>
#include <Rendering/Data/MaterialParser.h>
#include <Rendering/Resources/Texture.h>
#include <Rendering/Backend/BackendResources.h>

#include <Tools/Utils/OptRef.h>
#include "Core/Global/ServiceLocator.h"
#include "Core/ResourceManagement/TextureManager.h"

namespace
{
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
		if (auto value = as.operator() < Data::Texture* > ()) return *value;
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
			else if constexpr (std::same_as<T, Data::Texture*>) { return static_cast<Data::Texture*>(nullptr); }
			else if constexpr (std::same_as<T, Resources::Texture*>) { return static_cast<Resources::Texture*>(nullptr); }
			else { return std::monostate{}; }
		}, p_value);
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
}


Rendering::Data::MaterialInstance::MaterialInstance(Material& p_definition, bool p_isDefault)
	: m_material(&p_definition)
	, m_isDefaultInstance(p_isDefault)
{
	// A plain instance starts empty and inherits; the default instance is the definition's
	// own value set, seeded from the engine defaults here and from the shader in ResetProperties.
	if (p_isDefault)
	{
		InitializeDefaultState();
	}
	else if (m_material != nullptr)
	{
		// A plain instance inherits the material default's feature selection; a per-shape
		// instance must see the same feature set as the .mat it was created from.
		m_features = m_material->Features();
		ResetProperties();
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
	m_material = &p_material;
	m_isDefaultInstance = false;
	ResetStateToMaterial();
	// Clears every override, releases and rebuilds the descriptor set against the new
	// material's definition, and bumps the revision.
	ResetProperties();
}

void Rendering::Data::MaterialInstance::ResetStateToMaterial()
{
	InitializeDefaultState();

	if (m_material != nullptr && m_material->HasState())
	{
		const Material::State& state = m_material->GetState();
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
		if (parameter.isSampler && EngineDescriptorSets::IsGlobalSampler(parameter.name))
		{
			continue;
		}
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

	return snapshot;
}


std::optional<Rendering::Data::MaterialProperty>
Rendering::Data::MaterialInstance::GetProperty(const std::string p_key) const
{
	const MaterialDefinition* definition = Definition();
	uint32_t index = 0;
	const MaterialDefinition::Parameter* parameter =
		definition != nullptr ? definition->FindParameter(p_key, index) : nullptr;

	if (parameter != nullptr)
	{
		EnsureDescriptorSet();
		// Reading inherits any value this instance does not override.
		if (parameter->isSampler)
		{
			// The descriptor keeps both the GL handle and the serializable asset. Reporting the
			// asset first lets a .mat write the path it came from; a view-owned texture has no
			// asset, so the handle is what the editor sees.
			if (auto* asset = m_descriptorSet.GetTextureAsset(parameter->binding))
			{
				return MaterialProperty{ MaterialPropertyType{ asset } };
			}
			if (auto* handle = m_descriptorSet.GetTexture(parameter->binding))
			{
				return MaterialProperty{ MaterialPropertyType{ handle } };
			}
			return MaterialProperty{
				MaterialPropertyType{ static_cast<Rendering::Resources::Texture*>(nullptr) } };
		}

		//EnsureDescriptorSet();
		const MaterialDefinition::UniformField* field = definition->FindUniform(parameter->name);
		if (field != nullptr && m_uniformData.size() == definition->ParameterBlockSize())
		{
			const uint32_t offset = field->offset + (field->count > 1 ? field->stride * index : 0);
			return MaterialProperty{
				MaterialPropertyType{ ReadBlockValue(m_uniformData, field->type, offset) } };
		}
	}

	return std::nullopt;
}

Rendering::Data::Material* Rendering::Data::MaterialInstance::MaterialOf() const
{
	return m_material != nullptr ? m_material->MaterialOf() : nullptr;
}


const Rendering::Data::MaterialDefinition* Rendering::Data::MaterialInstance::Definition() const
{
	return m_material != nullptr ? m_material->Definition() : nullptr;
}
void Rendering::Data::MaterialInstance::UploadProperties(Rendering::Data::Texture* p_emptyTexture2D, Rendering::Data::Texture* p_emptyTextureCube, Rendering::Data::Texture* p_flatNormalTexture)
{
	ZoneScoped;

	// Packaged shaders carry their parameters as one std140 block (the per-material descriptor):
	// write the values by offset into it and bind it once, instead of resolving each name
	// against the program's reflected uniforms. A material without a definition has nothing to
	// upload: every value it can carry is a declared parameter of a package - the loose-uniform
	// path for source-built shaders is gone (docs/RenderThreadPlan.md §8.73).
	const MaterialDefinition* definition = Definition();
	if (definition != nullptr && definition->HasParameterTable())
	{
		UploadParameterBlock(p_emptyTexture2D, p_emptyTextureCube, p_flatNormalTexture);
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
				Descriptor::BufferType::Uniform, pool.GetHandle(),
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

	// Inheritance is merged by the bind/upload paths, not here: SetProperty must not copy the
	// whole default block again for every property it writes.
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
	const Rendering::Data::Program& p_program)
{
	EnsureDescriptorSet();

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
		uint32_t arrayIndex = 0;
		const MaterialDefinition::Parameter* parameter =
			definition->FindParameter(field.name, arrayIndex);
		if (parameter == nullptr)
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


void Rendering::Data::MaterialInstance::UploadParameterBlock(
	Rendering::Data::Texture* p_emptyTexture2D,
	Rendering::Data::Texture* p_emptyTextureCube,
	Rendering::Data::Texture* p_flatNormalTexture)
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

	// Samplers: only fill the slots that have no material texture/default yet. Textures set
	// through SetProperty are already in the descriptor set and persist
	// across uploads; rewriting them here on every upload was redundant.
	for (const MaterialDefinition::SamplerField& field : definition->SamplerFields())
	{
		// A sampler that names an asset is already in the descriptor: the asset's handle is what
		// travels, and the executor resolves it to the backend object when it binds the set
		// (Filament's descriptor set holds the handle). Resolving the pointer here would need the
		// backend object on the recording side, which does not exist yet - creation is a command.
		if (Rendering::Resources::Texture* asset = m_descriptorSet.GetTextureAsset(field.binding))
		{
			(void)asset;
			continue;
		}

		Rendering::Data::Texture* handle = m_descriptorSet.GetTexture(field.binding);
		if (handle != nullptr)
		{
			continue;
		}

		// A buffer texture has no 1x1 stand-in in the engine; binding the 2D fallback to a
		// samplerBuffer slot is a type error at draw time. An unassigned one is simply left
		// unbound (the shader that declares it does not read it without a buffer).
		if (field.type != "sampler2D" && field.type != "samplerCube")
		{
			m_descriptorSet.SetTexture(field.binding, nullptr);
			continue;
		}

		Rendering::Data::Texture* fallback =
			field.type == "samplerCube" ? p_emptyTextureCube : p_emptyTexture2D;
		const char* fallbackName = field.type == "samplerCube" ? "empty cube (black)" : "empty 2D (white)";
		if (field.fallback == static_cast<uint8_t>(Rendering::Data::MaterialParser::SamplerFallback::FlatNormal)
			&& p_flatNormalTexture != nullptr)
		{
			fallback = p_flatNormalTexture;
			fallbackName = "flat normal (0.5,0.5,1)";
		}
		if (fallback == nullptr)
		{
			continue;
		}

		m_descriptorSet.SetTexture(field.binding, fallback);

		// MOON_DEBUG_SAMPLERS: what each sampler slot actually got, once per shader and slot.
		// The "why is my texture ignored" question has two very different answers (the material
		// property is empty vs. it is set but the shader never reads it), and this says which.
		if (std::getenv("MOON_DEBUG_SAMPLERS") != nullptr)
		{
			static std::set<std::string> reported;
			const std::string key = (MaterialOf() != nullptr ? MaterialOf()->path : std::string("?")) + "|" + field.name;
			if (reported.insert(key).second)
			{
				std::cout << "[Sampler] " << (MaterialOf() != nullptr ? MaterialOf()->path : "?") << " "
					<< field.name << " binding " << field.binding << " <- "
					<< (handle != nullptr ? "material texture" : fallbackName) << std::endl;
			}
		}
	}
}


bool Rendering::Data::MaterialInstance::ResetPropertyToDefault(const std::string& p_name)
{
	const MaterialDefinition* definition = Definition();
	if (definition == nullptr)
	{
		return false;
	}

	uint32_t index = 0;
	const MaterialDefinition::Parameter* parameter = definition->FindParameter(p_name, index);
	if (parameter == nullptr)
	{
		return false;
	}

	EnsureDescriptorSet();
	if (parameter->isSampler)
	{
		Rendering::Data::Texture* handle = nullptr;
		Rendering::Resources::Texture* asset = nullptr;
		if (!m_isDefaultInstance && m_material != nullptr)
		{
			handle = m_material->DefaultInstance().GetTexture(parameter->binding);
			asset = m_material->DefaultInstance().GetTextureAsset(parameter->binding);
		}
		m_descriptorSet.SetTexture(parameter->binding, handle, asset);
		++m_revision;
		return true;
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

	if (!m_isDefaultInstance && m_material != nullptr)
	{
		const std::vector<uint8_t>& inherited = m_material->DefaultInstance().BufferData();
		const uint32_t size = field != nullptr ? BlockFieldSize(field->type) : 0;
		if (size > 0 && offset + size <= inherited.size() && offset + size <= block.size())
		{
			std::memcpy(block.data() + offset, inherited.data() + offset, size);
			++m_revision;
			return true;
		}
	}

	const MaterialPropertyType value = ParameterDefaultValue(*parameter);
	if (std::holds_alternative<std::monostate>(value))
	{
		return false;
	}
	WriteBlockValue(block, parameter->type, value, offset);
	++m_revision;
	return true;
}


void Rendering::Data::MaterialInstance::ResetProperties()
{
	ReleaseDescriptorSet();
	EnsureDescriptorSet();

	// A plain instance owns its own block and textures; resetting means taking the material's
	// default instance values once, not merging them on every frame.
	if (!m_isDefaultInstance && m_material != nullptr)
	{
		const MaterialInstance& defaults = m_material->DefaultInstance();
		if (defaults.BufferSize() == m_uniformData.size() && !defaults.BufferData().empty())
		{
			m_uniformData = defaults.BufferData();
		}
		if (const MaterialDefinition* definition = Definition())
		{
			for (const MaterialDefinition::SamplerField& field : definition->SamplerFields())
			{
				m_descriptorSet.SetTexture(field.binding,
					defaults.GetTexture(field.binding), defaults.GetTextureAsset(field.binding));
			}
		}
	}
	++m_revision;
}


void Rendering::Data::MaterialInstance::UpdateProperties()
{
	ResetProperties();
}


void Rendering::Data::MaterialInstance::Bind(
	std::optional<const std::string_view> p_pass,
	Tools::Utils::OptRef<const Data::FeatureSet> p_featureSetOverride
)
{
	ZoneScoped;
	Material* material = MaterialOf();
	if (material == nullptr)
	{
		return;
	}
	if (std::getenv("MOON_UNIFORMS") != nullptr)
	{
		// Which variant this bind asks for: the feature set decides the shader, and a set that
		// differs between runs explains "same draw state, different pixels" (docs §8.65).
		static int binds = 0;
		const auto& features = p_featureSetOverride.value_or(FeaturesOf());
		if (binds < 400 && !features.empty())
		{
			++binds;
			std::fprintf(stderr, "[mi] pass=%s features=",
				p_pass.has_value() ? std::string(p_pass.value()).c_str() : "<any>");
			for (const auto& feature : features) { std::fprintf(stderr, "%s;", feature.c_str()); }
			std::fprintf(stderr, "\n");
			std::fflush(stderr);
		}
	}

	auto& program = material->GetVariant(
		p_pass,
		p_featureSetOverride.value_or(FeaturesOf())
	);

	program.Bind();

	m_programInUse = program;

	// The parameter block is per material, not per program: bind it on every draw so it is the
	// one bound even when another material uploaded its own buffer since the last draw.
	// Loose uniforms are uploaded by UploadProperties(), which the renderer calls after this
	// method selected and bound the program.
	BindParameterBlock(program);
}


void Rendering::Data::MaterialInstance::Unbind(bool p_resetBoundProgram)
{
	if (p_resetBoundProgram && m_programInUse)
	{
		m_programInUse->Unbind();
	}

	m_programInUse.reset();
}


bool Rendering::Data::MaterialInstance::HasProperty(const std::string& p_name) const
{
	const MaterialDefinition* definition = Definition();
	uint32_t index = 0;
	return definition != nullptr && definition->FindParameter(p_name, index) != nullptr;
}


void Rendering::Data::MaterialInstance::SetProperty(const std::string p_name, const MaterialPropertyType& p_value)
{
	const MaterialDefinition* definition = Definition();
	uint32_t index = 0;
	const MaterialDefinition::Parameter* parameter =
		definition != nullptr ? definition->FindParameter(p_name, index) : nullptr;

	if (parameter != nullptr)
	{
		if (parameter->isSampler && EngineDescriptorSets::IsGlobalSampler(parameter->name))
		{
			// View-owned IBL textures are bound through the per-view set, not by materials.
			return;
		}
		EnsureDescriptorSet();
		if (parameter->isSampler)
		{
			Rendering::Data::Texture* handle = nullptr;
			Rendering::Resources::Texture* asset = nullptr;
			if (const auto* value = std::get_if<Rendering::Data::Texture*>(&p_value))
			{
				handle = *value;
			}
			else if (const auto* value = std::get_if<Rendering::Resources::Texture*>(&p_value))
			{
				// The asset is named by its handle: its HAL object is created by a command on the
				// executor, so asking for it here would assert (Filament stores the handle in the
				// descriptor and the driver resolves it).
				asset = *value;
			}
			m_descriptorSet.SetTexture(parameter->binding, handle, asset);
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
		}
	}
	else
	{
		// Not a parameter of the material's package: there is nowhere for the value to go. A
		// declared parameter lives in the per-material descriptor set; an engine view sampler
		// (_BRDFLut, _IrradianceCube, _PrefilterCube) is bound by the view's own set. The loose
		// path that used to upload these by name - a per-instance table plus glUniform on the
		// recording thread - is gone (docs/RenderThreadPlan.md §8.73). Reported once per material
		// and name, so a source-built shader that still sets its uniforms this way shows up here
		// instead of silently rendering nothing.
		if (std::holds_alternative<Rendering::Data::Texture*>(p_value)
			|| std::holds_alternative<Rendering::Resources::Texture*>(p_value))
		{
			if (Rendering::Data::EngineDescriptorSets::IsGlobalSampler(p_name))
			{
				return;
			}
		}
		static std::set<std::string> reported;
		const std::string materialPath = MaterialOf() != nullptr ? MaterialOf()->path : std::string("?");
		if (reported.insert(materialPath + "|" + p_name).second)
		{
			std::fprintf(stderr,
				"[material] %s: '%s' is not a parameter of the material: the value is ignored\n",
				materialPath.c_str(), p_name.c_str());
			std::fflush(stderr);
		}
	}

	++m_revision;
}


void Rendering::Data::MaterialInstance::LateUpdateTexture(const std::string& p_name, const std::string& p_path)
{
	(void)p_path;
	if (HasProperty(p_name))
	{
		++m_revision;
	}
}


bool Rendering::Data::MaterialInstance::TrySetProperty(const std::string& p_name, const MaterialPropertyType& p_value)
{
	if (HasProperty(p_name))
	{
		SetProperty(p_name, p_value);
		return true;
	}
	return false;
}


bool Rendering::Data::MaterialInstance::IsValid() const
{
	return MaterialOf() != nullptr;
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
	return m_blendable ? *m_blendable : (m_material ? m_material->IsBlendable() : false);
}


bool Rendering::Data::MaterialInstance::IsTransparent() const
{
	return m_transparent ? *m_transparent : (m_material ? m_material->IsTransparent() : false);
}


bool Rendering::Data::MaterialInstance::IsUserInterface() const
{
	return m_userInterface ? *m_userInterface : (m_material ? m_material->IsUserInterface() : false);
}


bool Rendering::Data::MaterialInstance::HasBackfaceCulling() const
{
	return m_backfaceCulling ? *m_backfaceCulling : (m_material ? m_material->HasBackfaceCulling() : true);
}


bool Rendering::Data::MaterialInstance::HasFrontfaceCulling() const
{
	return m_frontfaceCulling ? *m_frontfaceCulling : (m_material ? m_material->HasFrontfaceCulling() : false);
}


bool Rendering::Data::MaterialInstance::HasDepthTest() const
{
	return m_depthTest ? *m_depthTest : (m_material ? m_material->HasDepthTest() : true);
}


bool Rendering::Data::MaterialInstance::HasDepthWriting() const
{
	return m_depthWriting ? *m_depthWriting : (m_material ? m_material->HasDepthWriting() : true);
}


bool Rendering::Data::MaterialInstance::HasColorWriting() const
{
	return m_colorWriting ? *m_colorWriting : (m_material ? m_material->HasColorWriting() : true);
}


bool Rendering::Data::MaterialInstance::IsShadowCaster() const
{
	return m_castShadows ? *m_castShadows : (m_material ? m_material->IsShadowCaster() : true);
}


bool Rendering::Data::MaterialInstance::IsShadowReceiver() const
{
	return m_receiveShadows ? *m_receiveShadows : (m_material ? m_material->IsShadowReceiver() : true);
}


bool Rendering::Data::MaterialInstance::IsCapturedByReflectionProbes() const
{
	return m_capturedByReflectionProbes ? *m_capturedByReflectionProbes : (m_material ? m_material->IsCapturedByReflectionProbes() : true);
}


bool Rendering::Data::MaterialInstance::IsReflectionReceiver() const
{
	return m_receiveReflections ? *m_receiveReflections : (m_material ? m_material->IsReflectionReceiver() : true);
}


int Rendering::Data::MaterialInstance::GetDrawOrder() const
{
	return m_drawOrder ? *m_drawOrder : (m_material ? m_material->GetDrawOrder() : 1000);
}


int Rendering::Data::MaterialInstance::GetGPUInstances() const
{
	return m_gpuInstances ? *m_gpuInstances : (m_material ? m_material->GetGPUInstances() : 1);
}


float Rendering::Data::MaterialInstance::GetLineWidth() const
{
	return m_lineWidth ? *m_lineWidth : (m_material ? m_material->GetLineWidth() : 1.0f);
}


Rendering::Settings::EBlendMode Rendering::Data::MaterialInstance::GetBlendMode() const
{
	if (m_blendMode) { return *m_blendMode; }
	return m_material ? m_material->GetBlendMode() : Rendering::Settings::EBlendMode::BLEND_OPAQUE;
}


bool Rendering::Data::MaterialInstance::SupportsOrthographic() const
{
	return m_supportOrthographic ? *m_supportOrthographic : (m_material ? m_material->SupportsOrthographic() : true);
}


bool Rendering::Data::MaterialInstance::SupportsPerspective() const
{
	return m_supportPerspective ? *m_supportPerspective : (m_material ? m_material->SupportsPerspective() : true);
}

bool Rendering::Data::MaterialInstance::HasPolygonOffsetFill() const
{
	return m_polygonOffsetFill ? *m_polygonOffsetFill : (m_material ? m_material->HasPolygonOffsetFill() : false);
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


void Rendering::Data::MaterialInstance::CopyStateFrom(const MaterialInstance& p_other)
{
	// Every render-state field, in one place: the .mat's per-drawable instance is built from
	// this, and a hand-written copy list is exactly how the face polygon offset once went
	// missing - the faces were no longer pushed back, so a coplanar edge z-fought with them and
	// disappeared at concave intersections (picking kept working because its pass sets the
	// offset itself).
	m_supportOrthographic = p_other.m_supportOrthographic;
	m_supportPerspective = p_other.m_supportPerspective;
	m_userInterface = p_other.m_userInterface;
	m_blendable = p_other.m_blendable;
	m_blendMode = p_other.m_blendMode;
	m_transparent = p_other.m_transparent;
	m_backfaceCulling = p_other.m_backfaceCulling;
	m_frontfaceCulling = p_other.m_frontfaceCulling;
	m_polygonOffsetFill = p_other.m_polygonOffsetFill;
	m_depthTest = p_other.m_depthTest;
	m_depthWriting = p_other.m_depthWriting;
	m_colorWriting = p_other.m_colorWriting;
	m_castShadows = p_other.m_castShadows;
	m_receiveShadows = p_other.m_receiveShadows;
	m_capturedByReflectionProbes = p_other.m_capturedByReflectionProbes;
	m_receiveReflections = p_other.m_receiveReflections;
	m_gpuInstances = p_other.m_gpuInstances;
	m_drawOrder = p_other.m_drawOrder;
	m_lineWidth = p_other.m_lineWidth;
	m_features = p_other.m_features;
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
	const Material* material = MaterialOf();
	return material != nullptr && material->GetFeatures().contains(p_feature);
}


bool Rendering::Data::MaterialInstance::HasPass(const std::string& p_pass) const
{
	const Material* material = MaterialOf();
	return material != nullptr && material->GetPasses().contains(p_pass);
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

