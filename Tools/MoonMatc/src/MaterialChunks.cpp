#include "MaterialChunks.h"

namespace Moon::Matc
{
	void DictionaryTextChunk::Flatten(Flattener& p_flattener) const
	{
		p_flattener.WriteUint32(static_cast<uint32_t>(m_lines.size()));
		for (const std::string& line : m_lines)
		{
			p_flattener.WriteString(line);
		}
	}

	void MaterialStageTextChunk::Flatten(Flattener& p_flattener) const
	{
		p_flattener.WriteUint32(static_cast<uint32_t>(m_stages.size()));
		for (const auto& [stage, ids] : m_stages)
		{
			p_flattener.WriteString(stage);
			p_flattener.WriteUint32(static_cast<uint32_t>(ids.size()));
			for (const uint32_t id : ids)
			{
				p_flattener.WriteUint32(id);
			}
		}
	}

	void MaterialGlslChunk::Flatten(Flattener& p_flattener) const
	{
		p_flattener.WriteUint32(static_cast<uint32_t>(m_entries.size()));
		for (const auto& [key, source] : m_entries)
		{
			p_flattener.WriteString(key);
			p_flattener.WriteString(source);
		}
	}

	void MaterialVariantTableChunk::Flatten(Flattener& p_flattener) const
	{
		p_flattener.WriteUint32(static_cast<uint32_t>(m_variants.size()));
		for (const PassVariant& variant : m_variants)
		{
			p_flattener.WriteString(variant.pass);
			p_flattener.WriteUint32(static_cast<uint32_t>(variant.features.size()));
			for (const std::string& feature : variant.features)
			{
				p_flattener.WriteString(feature);
			}
		}
	}

	void MaterialFeatureSetChunk::Flatten(Flattener& p_flattener) const
	{
		p_flattener.WriteUint32(static_cast<uint32_t>(m_features.size()));
		for (const std::string& feature : m_features)
		{
			p_flattener.WriteString(feature);
		}
	}

	void MaterialPropertiesChunk::Flatten(Flattener& p_flattener) const
	{
		p_flattener.WriteUint32(static_cast<uint32_t>(m_parameters.size()));
		for (const Parameter& parameter : m_parameters)
		{
			const uint8_t flags =
				(parameter.hasRange ? 0x01 : 0x00) |
				(parameter.hasStep ? 0x02 : 0x00) |
				(parameter.color ? 0x04 : 0x00) |
				(parameter.ui ? 0x00 : 0x08);

			p_flattener.WriteString(parameter.name);
			p_flattener.WriteUint8(static_cast<uint8_t>(parameter.type));
			p_flattener.WriteUint32(parameter.count);
			p_flattener.WriteString(parameter.defaultExpression);
			p_flattener.WriteString(parameter.label);
			p_flattener.WriteString(parameter.group);
			p_flattener.WriteUint8(flags);
			p_flattener.WriteFloat(parameter.min);
			p_flattener.WriteFloat(parameter.max);
			p_flattener.WriteFloat(parameter.step);
			p_flattener.WriteUint32(parameter.offset);
			p_flattener.WriteUint32(parameter.stride);
			p_flattener.WriteUint32(parameter.binding);
		}
	}

	void MaterialAttributeInfoChunk::Flatten(Flattener& p_flattener) const
	{
		p_flattener.WriteUint32(static_cast<uint32_t>(m_attributes.size()));
		for (const VertexAttribute& attribute : m_attributes)
		{
			p_flattener.WriteUint32(attribute.location);
			p_flattener.WriteString(attribute.glslType);
			p_flattener.WriteString(attribute.name);
		}
	}

	void MaterialUniformInterfaceBlockChunk::Flatten(Flattener& p_flattener) const
	{
		p_flattener.WriteString(m_ubo.name);
		p_flattener.WriteUint32(m_ubo.size);
		p_flattener.WriteUint32(static_cast<uint32_t>(m_ubo.uniforms.size()));
		for (const UniformInfo& uniform : m_ubo.uniforms)
		{
			p_flattener.WriteString(uniform.name);
			p_flattener.WriteUint8(static_cast<uint8_t>(uniform.type));
			p_flattener.WriteUint32(uniform.offset);
			p_flattener.WriteUint32(uniform.stride);
			p_flattener.WriteUint32(uniform.count);
		}
	}

	void MaterialSamplerInterfaceBlockChunk::Flatten(Flattener& p_flattener) const
	{
		p_flattener.WriteString(m_samplers.name);
		p_flattener.WriteUint32(static_cast<uint32_t>(m_samplers.samplers.size()));
		for (const SamplerInfo& sampler : m_samplers.samplers)
		{
			p_flattener.WriteString(sampler.name);
			p_flattener.WriteUint8(static_cast<uint8_t>(sampler.type));
			p_flattener.WriteUint8(static_cast<uint8_t>(sampler.fallback));
			p_flattener.WriteUint32(sampler.binding);
		}
	}

	void MaterialStateChunk::Flatten(Flattener& p_flattener) const
	{
		p_flattener.WriteUint32(static_cast<uint32_t>(m_state.size()));
		for (const auto& [key, value] : m_state)
		{
			p_flattener.WriteString(key);
			p_flattener.WriteString(value);
		}
	}
}
