#include <Rendering/Resources/ShaderPackage.h>

#include <cstring>
#include <iostream>

namespace
{
	constexpr char kMagic[8] = { 'M', 'O', 'O', 'N', 'S', 'D', 'R', '1' };

	/** Reads one little-endian field out of a chunk payload and moves the cursor past it. */
	template<typename T>
	bool Take(std::string_view& p_cursor, T& p_out)
	{
		if (p_cursor.size() < sizeof(T))
		{
			return false;
		}
		std::memcpy(&p_out, p_cursor.data(), sizeof(T));
		p_cursor.remove_prefix(sizeof(T));
		return true;
	}

	bool TakeString(std::string_view& p_cursor, std::string& p_out)
	{
		uint32_t size = 0;
		if (!Take(p_cursor, size) || p_cursor.size() < size)
		{
			return false;
		}
		p_out.assign(p_cursor.data(), size);
		p_cursor.remove_prefix(size);
		return true;
	}

	bool TakeView(std::string_view& p_cursor, std::string_view& p_out)
	{
		uint32_t size = 0;
		if (!Take(p_cursor, size) || p_cursor.size() < size)
		{
			return false;
		}
		p_out = p_cursor.substr(0, size);
		p_cursor.remove_prefix(size);
		return true;
	}
}

namespace Rendering::Resources
{
	bool ShaderPackage::IsPackage(const void* p_data, uint32_t p_size)
	{
		return p_data != nullptr && p_size >= sizeof(kMagic)
			&& std::memcmp(p_data, kMagic, sizeof(kMagic)) == 0;
	}

	ShaderPackage::ShaderPackage(const void* p_data, uint32_t p_size)
	{
		if (!IsPackage(p_data, p_size))
		{
			return;
		}

		std::string_view remaining(static_cast<const char*>(p_data) + sizeof(kMagic), p_size - sizeof(kMagic));

		while (remaining.size() >= 12)
		{
			const std::string_view tag = remaining.substr(0, 8);
			remaining.remove_prefix(8);

			uint32_t size = 0;
			if (!Take(remaining, size) || remaining.size() < size)
			{
				break;
			}
			std::string_view payload = remaining.substr(0, size);
			remaining.remove_prefix(size);

			if (tag == "SHDRNAME")
			{
				m_name.assign(payload);
			}
			else if (tag == "SHDRVARI")
			{
				uint32_t count = 0;
				if (!Take(payload, count))
				{
					continue;
				}
				for (uint32_t i = 0; i < count; ++i)
				{
					Variant variant;
					uint32_t featureCount = 0;
					if (!TakeString(payload, variant.pass) || !Take(payload, featureCount))
					{
						break;
					}
					for (uint32_t f = 0; f < featureCount; ++f)
					{
						std::string feature;
						if (!TakeString(payload, feature))
						{
							break;
						}
						variant.features.insert(feature);
					}
					m_variants.push_back(std::move(variant));
				}
			}
			else if (tag == "SHDRFEAT")
			{
				uint32_t count = 0;
				if (!Take(payload, count))
				{
					continue;
				}
				for (uint32_t i = 0; i < count; ++i)
				{
					std::string feature;
					if (!TakeString(payload, feature))
					{
						break;
					}
					m_declaredFeatures.push_back(std::move(feature));
				}
			}
			else if (tag == "SHDRPROP")
			{
				uint32_t count = 0;
				if (!Take(payload, count))
				{
					continue;
				}
				m_parameters.clear();
				for (uint32_t i = 0; i < count; ++i)
				{
					Parameter parameter;
					if (!TakeString(payload, parameter.name)
						|| !TakeString(payload, parameter.type)
						|| !TakeString(payload, parameter.glslType)
						|| !Take(payload, parameter.count))
					{
						break;
					}
					uint8_t sampler = 0;
					if (!Take(payload, sampler) || !TakeString(payload, parameter.defaultValue))
					{
						break;
					}
					parameter.isSampler = sampler != 0;

					// The designer fields were added after the first packages were written; a
					// package from before them simply ends here and keeps the fallbacks.
					if (!payload.empty())
					{
						uint8_t flags = 0;
						if (!TakeString(payload, parameter.label)
							|| !TakeString(payload, parameter.group)
							|| !Take(payload, parameter.min)
							|| !Take(payload, parameter.max)
							|| !Take(payload, parameter.step)
							|| !Take(payload, flags))
						{
							break;
						}
						parameter.hasRange = (flags & 0x01) != 0;
						parameter.hasStep = (flags & 0x02) != 0;
						parameter.isColor = (flags & 0x04) != 0;
						parameter.isUi = (flags & 0x08) == 0;

						// The descriptor layout came after the designer fields: packages from
						// between the two carry none and keep the loose-uniform path.
						if (payload.size() >= sizeof(uint32_t) * 3)
						{
							if (!Take(payload, parameter.offset)
								|| !Take(payload, parameter.stride)
								|| !Take(payload, parameter.binding))
							{
								break;
							}
						}
					}
					m_parameters.push_back(std::move(parameter));
				}

				// The chunk ends with the std140 size of the block (new packages only).
				if (payload.size() >= sizeof(uint32_t))
				{
					Take(payload, m_parameterBlockSize);
				}
			}
			else if (tag == "SHDRFALL")
			{
				uint32_t count = 0;
				if (!Take(payload, count))
				{
					continue;
				}
				for (uint32_t i = 0; i < count; ++i)
				{
					SamplerFallbackEntry entry;
					uint8_t kind = 0;
					if (!TakeString(payload, entry.name) || !Take(payload, kind))
					{
						break;
					}
					entry.kind = static_cast<SamplerFallback>(kind);
					m_samplerFallbacks.push_back(std::move(entry));
				}
			}
			else if (tag == "SHDRSTAT")
			{
				uint32_t count = 0;
				if (!Take(payload, count))
				{
					continue;
				}
				for (uint32_t i = 0; i < count; ++i)
				{
					std::string key;
					std::string value;
					if (!TakeString(payload, key) || !TakeString(payload, value))
					{
						break;
					}
					m_states[std::move(key)] = std::move(value);
				}
			}
			else if (tag == "SHDRATTR")
			{
				uint32_t count = 0;
				if (!Take(payload, count))
				{
					continue;
				}
				for (uint32_t i = 0; i < count; ++i)
				{
					Attribute attribute;
					if (!Take(payload, attribute.location)
						|| !TakeString(payload, attribute.glslType)
						|| !TakeString(payload, attribute.name))
					{
						break;
					}
					m_attributes.push_back(std::move(attribute));
				}
			}
			else if (tag == "SHDRPARM")
			{
				// The old name/type table. A package also carries SHDRPROP, which is written
				// after this chunk and replaces these entries with the full records; a package
				// from before SHDRPROP existed just keeps what is here.
				uint32_t count = 0;
				if (!Take(payload, count))
				{
					continue;
				}
				for (uint32_t i = 0; i < count; ++i)
				{
					Parameter parameter;
					if (!TakeString(payload, parameter.name) || !TakeString(payload, parameter.type))
					{
						break;
					}
					m_parameters.push_back(std::move(parameter));
				}
			}
			else if (tag == "SHDRSTAG")
			{
				uint32_t count = 0;
				if (!Take(payload, count))
				{
					continue;
				}
				for (uint32_t i = 0; i < count; ++i)
				{
					std::string stage;
					std::string_view text;
					if (!TakeString(payload, stage) || !TakeView(payload, text))
					{
						break;
					}
					m_stageTexts[stage] = text;
				}
			}
			else if (tag == "SHDRGLSL")
			{
				uint32_t count = 0;
				if (!Take(payload, count))
				{
					continue;
				}
				for (uint32_t i = 0; i < count; ++i)
				{
					Blob blob;
					if (!TakeView(payload, blob.name) || !TakeView(payload, blob.source))
					{
						break;
					}
					m_blobs.push_back(blob);
				}
			}
		}

		m_valid = !m_variants.empty() && !m_blobs.empty();
		if (!m_valid)
		{
			std::cerr << "[ShaderPackage] the package is missing its variants or its sources" << std::endl;
		}
	}

	namespace
	{
		/** The blob key MoonMatc writes: "pass/feature|feature/stage" - the separators sit
		 * between the features, not after each of them. */
		std::string BlobKey(const ShaderPackage::Variant& p_variant, ShaderPackage::Stage p_stage)
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
			case ShaderPackage::Stage::Vertex: key += "vertex"; break;
			case ShaderPackage::Stage::Fragment: key += "fragment"; break;
			default: key += "geometry"; break;
			}
			return key;
		}
	}

	const ShaderPackage::Blob* ShaderPackage::FindBlob(const Variant& p_variant, Stage p_stage) const
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

	std::string ShaderPackage::Source(const Variant& p_variant, Stage p_stage) const
	{
		const Blob* blob = FindBlob(p_variant, p_stage);
		if (blob == nullptr)
		{
			return {};
		}

		// Stage name -> the text every variant shares, with a placeholder for the defines this
		// variant contributes. A package from before the shared-stage chunk has no such text
		// and its blobs are whole stages, so it keeps working unchanged.
		const std::string_view stageName = [p_stage] {
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
			return std::string{ blob->source };
		}

		std::string text{ shared->second };
		const size_t at = text.find(kShaderDefinesPlaceholder);
		if (at != std::string::npos)
		{
			text.replace(at, kShaderDefinesPlaceholder.size(), blob->source);
		}
		return text;
	}
}
