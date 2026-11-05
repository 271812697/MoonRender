#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>

namespace Rendering::Resources
{
	/** Reads the little-endian fields a Flattener wrote, with a boundary check on every read
	 * (the package is untrusted input; a truncated chunk must not read past its payload). */
	class Unflattener
	{
	public:
		Unflattener() = default;
		Unflattener(const uint8_t* p_src, size_t p_size)
			: m_src(p_src), m_cursor(p_src), m_end(p_src + p_size) {}

		bool HasData() const noexcept { return m_cursor < m_end; }

		template<typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
		bool Read(T* p_out) noexcept
		{
			if (m_cursor + sizeof(T) > m_end || p_out == nullptr)
			{
				return false;
			}

			T value = 0;
			for (size_t i = 0; i < sizeof(T); ++i)
			{
				value |= static_cast<T>(m_cursor[i]) << (8 * i);
			}
			*p_out = value;
			m_cursor += sizeof(T);
			return true;
		}

		bool ReadFloat(float* p_out) noexcept
		{
			uint32_t bits = 0;
			if (!Read(&bits))
			{
				return false;
			}
			std::memcpy(p_out, &bits, sizeof(bits));
			return true;
		}

		bool ReadStringView(std::string_view& p_out) noexcept
		{
			const uint8_t* start = m_cursor;
			while (m_cursor < m_end && *m_cursor != '\0')
			{
				++m_cursor;
			}
			if (m_cursor >= m_end)
			{
				return false;
			}

			p_out = std::string_view(reinterpret_cast<const char*>(start),
				static_cast<size_t>(m_cursor - start));
			++m_cursor; // skip the NUL
			return true;
		}

		bool ReadString(std::string& p_out) noexcept
		{
			std::string_view view;
			if (!ReadStringView(view))
			{
				return false;
			}
			p_out.assign(view);
			return true;
		}

		/** A uint64 length followed by that many bytes (filament's writeBlob). */
		bool ReadBlobView(std::string_view& p_out) noexcept
		{
			uint64_t size = 0;
			if (!Read(&size) || size > static_cast<uint64_t>(m_end - m_cursor))
			{
				return false;
			}
			p_out = std::string_view(reinterpret_cast<const char*>(m_cursor),
				static_cast<size_t>(size));
			m_cursor += size;
			return true;
		}

		bool Skip(size_t p_size) noexcept
		{
			if (p_size > static_cast<size_t>(m_end - m_cursor))
			{
				return false;
			}
			m_cursor += p_size;
			return true;
		}

		const uint8_t* GetCursor() const noexcept { return m_cursor; }

	private:
		const uint8_t* m_src = nullptr;
		const uint8_t* m_cursor = nullptr;
		const uint8_t* m_end = nullptr;
	};
}
