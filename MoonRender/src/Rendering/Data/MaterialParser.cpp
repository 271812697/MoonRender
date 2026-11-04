#include <Rendering/Settings/EBindingPoint.h>
#include <Rendering/Data/MaterialParser.h>
#include <Rendering/Resources/MaterialChunkType.h>
#include <Rendering/Resources/MaterialParameterType.h>
#include <Rendering/Resources/Unflattener.h>

#include <cstring>
#include <iostream>

namespace Rendering::Data
{
	namespace
	{
		using Moon::Material::ChunkType;
		using Moon::Material::ParameterType;

		const Rendering::Resources::ChunkContainer::Chunk* ChunkOf(const Rendering::Resources::ChunkContainer& p_container, ChunkType p_type)
		{
			return p_container.GetChunk(p_type);
		}

		bool ReadString(Rendering::Resources::Unflattener& p_reader, std::string& p_out)
		{
			return p_reader.ReadString(p_out);
		}

		/** The shared-stage text's placeholder: the variant's define lines are spliced there. */
		constexpr std::string_view kDefinesPlaceholder = "//@MOON_VARIANT_DEFINES@";
	}

	bool MaterialParser::IsPackage(const void* p_data, uint32_t p_size)
	{
		if (p_data == nullptr || p_size < sizeof(uint64_t) + sizeof(uint32_t))
		{
			return false;
		}

		uint64_t type = 0;
		uint32_t size = 0;
		std::memcpy(&type, p_data, sizeof(type));
		std::memcpy(&size, static_cast<const uint8_t*>(p_data) + sizeof(type), sizeof(size));
		return static_cast<ChunkType>(type) == ChunkType::MaterialVersion
			&& size <= p_size - sizeof(type) - sizeof(size);
	}

	MaterialParser::MaterialParser(const void* p_data, uint32_t p_size)
		: m_container(p_data, p_size)
	{
		if (!m_container.Parse())
		{
			std::cerr << "[MaterialParser] the chunk list is truncated or empty" << std::endl;
			return;
		}

		// MAT_VERS / MAT_NAME / MAT_UUID: what this package is.
		if (const Rendering::Resources::ChunkContainer::Chunk* version = ChunkOf(m_container, ChunkType::MaterialVersion))
		{
			Rendering::Resources::Unflattener reader(version->start, version->size);
			uint32_t number = 0;
			if (reader.Read(&number) && number != 3)
			{
				std::cerr << "[MaterialParser] unsupported package version " << number << std::endl;
			}
		}
		if (const Rendering::Resources::ChunkContainer::Chunk* name = ChunkOf(m_container, ChunkType::MaterialName))
		{
			Rendering::Resources::Unflattener reader(name->start, name->size);
			ReadString(reader, m_name);
		}

		// DIC_TEXT: the unique line table the stage texts are encoded against.
		if (const Rendering::Resources::ChunkContainer::Chunk* dictionary = ChunkOf(m_container, ChunkType::DictionaryText))
		{
			Rendering::Resources::Unflattener reader(dictionary->start, dictionary->size);
			uint32_t count = 0;
			if (reader.Read(&count))
			{
				m_lines.reserve(count);
				for (uint32_t i = 0; i < count; ++i)
				{
					std::string line;
					if (!ReadString(reader, line))
					{
						break;
					}
					m_lines.push_back(std::move(line));
				}
			}
		}

		// MON_STAG: each stage's whole text, reconstructed from its line ids.
		if (const Rendering::Resources::ChunkContainer::Chunk* stages = ChunkOf(m_container, ChunkType::MoonStageText))
		{
			Rendering::Resources::Unflattener reader(stages->start, stages->size);
			uint32_t count = 0;
			if (reader.Read(&count))
			{
				for (uint32_t i = 0; i < count; ++i)
				{
					std::string stage;
					uint32_t lineCount = 0;
					if (!ReadString(reader, stage) || !reader.Read(&lineCount))
					{
						break;
					}

					std::string text;
					for (uint32_t line = 0; line < lineCount; ++line)
					{
						uint32_t id = 0;
						if (!reader.Read(&id) || id >= m_lines.size())
						{
							break;
						}
						if (line != 0) { text += '\n'; }
						text += m_lines[id];
					}
					m_stageTexts[std::move(stage)] = std::move(text);
				}
			}
		}

		// MAT_GLSL: one entry per (pass, feature set, stage), carrying the variant's #define
		// lines. The stage text itself is shared in MON_STAG.
		if (const Rendering::Resources::ChunkContainer::Chunk* glsl = ChunkOf(m_container, ChunkType::MaterialGlsl))
		{
			Rendering::Resources::Unflattener reader(glsl->start, glsl->size);
			uint32_t count = 0;
			if (reader.Read(&count))
			{
				m_blobs.reserve(count);
				for (uint32_t i = 0; i < count; ++i)
				{
					Blob blob;
					if (!ReadString(reader, blob.name) || !ReadString(reader, blob.source))
					{
						break;
					}
					m_blobs.push_back(std::move(blob));
				}
			}
		}

		// MON_VARI: the variant table.
		if (const Rendering::Resources::ChunkContainer::Chunk* variants = ChunkOf(m_container, ChunkType::MoonVariantTable))
		{
			Rendering::Resources::Unflattener reader(variants->start, variants->size);
			uint32_t count = 0;
			if (reader.Read(&count))
			{
				m_variants.reserve(count);
				for (uint32_t i = 0; i < count; ++i)
				{
					Variant variant;
					uint32_t featureCount = 0;
					if (!ReadString(reader, variant.pass) || !reader.Read(&featureCount))
					{
						break;
					}
					for (uint32_t f = 0; f < featureCount; ++f)
					{
						std::string feature;
						if (!ReadString(reader, feature))
						{
							break;
						}
						variant.features.insert(std::move(feature));
					}
					m_variants.push_back(std::move(variant));
				}
			}
		}

		// MON_FEAT: the features the material declared.
		if (const Rendering::Resources::ChunkContainer::Chunk* features = ChunkOf(m_container, ChunkType::MoonFeatureSet))
		{
			Rendering::Resources::Unflattener reader(features->start, features->size);
			uint32_t count = 0;
			if (reader.Read(&count))
			{
				m_declaredFeatures.reserve(count);
				for (uint32_t i = 0; i < count; ++i)
				{
					std::string feature;
					if (!ReadString(reader, feature))
					{
						break;
					}
					m_declaredFeatures.push_back(std::move(feature));
				}
			}
		}

		// MAT_PROP: the reflected parameters. Type and count are typed; only the name / label /
		// group and the GLSL default expression are text.
		if (const Rendering::Resources::ChunkContainer::Chunk* properties = ChunkOf(m_container, ChunkType::MaterialProperties))
		{
			Rendering::Resources::Unflattener reader(properties->start, properties->size);
			uint32_t count = 0;
			if (reader.Read(&count))
			{
				m_parameters.reserve(count);
				for (uint32_t i = 0; i < count; ++i)
				{
					Parameter parameter;
					uint8_t type = 0;
					uint8_t flags = 0;
					if (!ReadString(reader, parameter.name)
						|| !reader.Read(&type)
						|| !reader.Read(&parameter.count)
						|| !ReadString(reader, parameter.defaultValue)
						|| !ReadString(reader, parameter.label)
						|| !ReadString(reader, parameter.group)
						|| !reader.Read(&flags)
						|| !reader.ReadFloat(&parameter.min)
						|| !reader.ReadFloat(&parameter.max)
						|| !reader.ReadFloat(&parameter.step))
					{
						break;
					}

					const ParameterType parameterType = static_cast<ParameterType>(type);
					parameter.type = Moon::Material::ParameterTypeName(parameterType);
					parameter.glslType = Moon::Material::ParameterTypeGlsl(parameterType);
					parameter.hasRange = (flags & 0x01) != 0;
					parameter.hasStep = (flags & 0x02) != 0;
					parameter.isColor = (flags & 0x04) != 0;
					parameter.isUi = (flags & 0x08) == 0;
					parameter.isSampler = Moon::Material::IsSamplerType(parameterType);

					if (!reader.Read(&parameter.offset) || !reader.Read(&parameter.stride)
						|| !reader.Read(&parameter.binding))
					{
						break;
					}
					m_parameters.push_back(std::move(parameter));
				}
			}
		}

		// MAT_ATTR: the vertex attributes the material reads.
		if (const Rendering::Resources::ChunkContainer::Chunk* attributes = ChunkOf(m_container, ChunkType::MaterialAttributeInfo))
		{
			Rendering::Resources::Unflattener reader(attributes->start, attributes->size);
			uint32_t count = 0;
			if (reader.Read(&count))
			{
				m_attributes.reserve(count);
				for (uint32_t i = 0; i < count; ++i)
				{
					Attribute attribute;
					if (!reader.Read(&attribute.location)
						|| !ReadString(reader, attribute.glslType)
						|| !ReadString(reader, attribute.name))
					{
						break;
					}
					m_attributes.push_back(std::move(attribute));
				}
			}
		}

		// MAT_UIB: the std140 block layout. The block size is the one field filament derives
		// from the field list; we store it because the runtime allocates exactly that range.
		if (const Rendering::Resources::ChunkContainer::Chunk* ubo = ChunkOf(m_container, ChunkType::MaterialUib))
		{
			Rendering::Resources::Unflattener reader(ubo->start, ubo->size);
			std::string name;
			uint32_t blockSize = 0;
			uint32_t count = 0;
			if (ReadString(reader, name) && reader.Read(&blockSize) && reader.Read(&count))
			{
				m_uniformBlockSize = blockSize;
				m_parameterBlockSize = blockSize;
				m_layout.Add(name.empty() ? "MaterialParams" : name,
					::Rendering::Data::DescriptorSetLayout::Kind::UniformBuffer,
					::Rendering::Settings::kMaterialParameterBinding);

				m_uniformFields.reserve(count);
				for (uint32_t i = 0; i < count; ++i)
				{
					UniformField field;
					uint8_t type = 0;
					if (!ReadString(reader, field.name) || !reader.Read(&type)
						|| !reader.Read(&field.offset) || !reader.Read(&field.stride)
						|| !reader.Read(&field.count))
					{
						break;
					}
					field.type = Moon::Material::ParameterTypeGlsl(static_cast<ParameterType>(type));
					m_uniformFields.push_back(std::move(field));
				}
			}
		}

		// MAT_SIB: one descriptor per sampler.
		if (const Rendering::Resources::ChunkContainer::Chunk* sib = ChunkOf(m_container, ChunkType::MaterialSib))
		{
			Rendering::Resources::Unflattener reader(sib->start, sib->size);
			std::string name;
			uint32_t count = 0;
			if (ReadString(reader, name) && reader.Read(&count))
			{
				m_samplerFields.reserve(count);
				for (uint32_t i = 0; i < count; ++i)
				{
					SamplerField field;
					uint8_t type = 0;
					uint8_t fallback = 0;
					if (!ReadString(reader, field.name) || !reader.Read(&type)
						|| !reader.Read(&fallback) || !reader.Read(&field.binding))
					{
						break;
					}
					field.type = Moon::Material::ParameterTypeName(static_cast<ParameterType>(type));
					field.fallback = fallback;
					m_samplerFields.push_back(field);
					m_layout.Add(field.name, ::Rendering::Data::DescriptorSetLayout::Kind::Texture,
						field.binding);
					m_samplerFallbacks.push_back(SamplerFallbackEntry{
						field.name, static_cast<MaterialParser::SamplerFallback>(fallback) });
				}
			}
		}

		// MON_STAT: the material block's render state.
		if (const Rendering::Resources::ChunkContainer::Chunk* state = ChunkOf(m_container, ChunkType::MoonMaterialState))
		{
			Rendering::Resources::Unflattener reader(state->start, state->size);
			uint32_t count = 0;
			if (reader.Read(&count))
			{
				for (uint32_t i = 0; i < count; ++i)
				{
					std::string key;
					std::string value;
					if (!ReadString(reader, key) || !ReadString(reader, value))
					{
						break;
					}
					m_states[std::move(key)] = std::move(value);
				}
			}
		}

		// Apply the block layout to the reflection records, so a consumer cannot see an offset
		// that disagrees with MAT_UIB / MAT_SIB.
		for (Parameter& parameter : m_parameters)
		{
			for (const UniformField& field : m_uniformFields)
			{
				if (field.name != parameter.name) { continue; }
				parameter.offset = static_cast<int32_t>(field.offset);
				parameter.stride = field.stride;
				parameter.count = field.count > 1 ? field.count : 0;
				break;
			}
			for (const SamplerField& field : m_samplerFields)
			{
				if (field.name != parameter.name) { continue; }
				parameter.binding = field.binding;
				break;
			}
		}

		m_valid = !m_variants.empty() && !m_blobs.empty();
		if (!m_valid)
		{
			std::cerr << "[MaterialParser] the package is missing its variants or its sources" << std::endl;
		}
	}

	namespace
	{
		/** The blob key MoonMatc writes: "pass/feature|feature/stage" - the separators sit
		 * between the features, not after each of them. */
		std::string BlobKey(const MaterialParser::Variant& p_variant, MaterialParser::Stage p_stage)
		{
			std::string key = p_variant.pass + "/";
			bool first = true;
			for (const std::string& feature : p_variant.features)
			{
				if (!first)
				{
					key += "|";
				}
				key += feature;
				first = false;
			}
			key += "/";
			switch (p_stage)
			{
			case MaterialParser::Stage::Vertex: key += "vertex"; break;
			case MaterialParser::Stage::Fragment: key += "fragment"; break;
			default: key += "geometry"; break;
			}
			return key;
		}
	}

	const MaterialParser::Blob* MaterialParser::FindBlob(const Variant& p_variant, Stage p_stage) const
	{
		const std::string key = BlobKey(p_variant, p_stage);
		for (const Blob& blob : m_blobs)
		{
			if (blob.name == key)
			{
				return &blob;
			}
		}
		return nullptr;
	}

	std::string MaterialParser::Source(const Variant& p_variant, Stage p_stage) const
	{
		const Blob* blob = FindBlob(p_variant, p_stage);
		if (blob == nullptr)
		{
			return {};
		}

		const std::string_view stageName = [p_stage]
		{
			switch (p_stage)
			{
			case Stage::Vertex: return std::string_view{ "vertex" };
			case Stage::Fragment: return std::string_view{ "fragment" };
			default: return std::string_view{ "geometry" };
			}
		}();

		const auto shared = m_stageTexts.find(std::string{ stageName });
		if (shared == m_stageTexts.end())
		{
			return blob->source;
		}

		std::string text = shared->second;
		const size_t at = text.find(kDefinesPlaceholder);
		if (at != std::string::npos)
		{
			text.replace(at, kDefinesPlaceholder.size(), blob->source);
		}
		return text;
	}
}
