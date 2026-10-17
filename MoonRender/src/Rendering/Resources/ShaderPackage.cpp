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
			else if (tag == "SHDRPARM")
			{
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
		std::string BlobKey(const ShaderPackage::Variant& p_variant, bool p_vertex)
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
			key += (p_vertex ? "vertex" : "fragment");
			return key;
		}
	}

	const ShaderPackage::Blob* ShaderPackage::FindBlob(const Variant& p_variant, bool p_vertex) const
	{
		const std::string key = BlobKey(p_variant, p_vertex);
		for (const Blob& blob : m_blobs)
		{
			if (blob.name == key)
			{
				return &blob;
			}
		}
		return nullptr;
	}

	std::string_view ShaderPackage::Source(const Variant& p_variant, bool p_vertex) const
	{
		const Blob* blob = FindBlob(p_variant, p_vertex);
		return blob != nullptr ? blob->source : std::string_view{};
	}
}
