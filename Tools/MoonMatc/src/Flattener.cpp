#include "Flattener.h"

#include <cstring>

namespace Moon::Matc
{
	void Flattener::WriteUint8(uint8_t p_value)
	{
		m_out.push_back(p_value);
	}

	void Flattener::WriteUint16(uint16_t p_value)
	{
		m_out.push_back(static_cast<uint8_t>(p_value & 0xFF));
		m_out.push_back(static_cast<uint8_t>((p_value >> 8) & 0xFF));
	}

	void Flattener::WriteUint32(uint32_t p_value)
	{
		m_out.push_back(static_cast<uint8_t>(p_value & 0xFF));
		m_out.push_back(static_cast<uint8_t>((p_value >> 8) & 0xFF));
		m_out.push_back(static_cast<uint8_t>((p_value >> 16) & 0xFF));
		m_out.push_back(static_cast<uint8_t>((p_value >> 24) & 0xFF));
	}

	void Flattener::WriteUint64(uint64_t p_value)
	{
		for (uint32_t i = 0; i < 8; ++i)
		{
			m_out.push_back(static_cast<uint8_t>((p_value >> (8 * i)) & 0xFF));
		}
	}

	void Flattener::WriteFloat(float p_value)
	{
		uint32_t bits = 0;
		std::memcpy(&bits, &p_value, sizeof(bits));
		WriteUint32(bits);
	}

	void Flattener::WriteString(std::string_view p_text)
	{
		m_out.insert(m_out.end(), p_text.begin(), p_text.end());
		m_out.push_back(0);
	}

	void Flattener::WriteBlob(const void* p_data, size_t p_size)
	{
		WriteUint64(p_size);
		const auto* bytes = static_cast<const uint8_t*>(p_data);
		m_out.insert(m_out.end(), bytes, bytes + p_size);
	}

	size_t Flattener::BeginSize()
	{
		const size_t position = m_out.size();
		WriteUint32(0);
		return position;
	}

	void Flattener::EndSize(size_t p_position)
	{
		const uint32_t size = static_cast<uint32_t>(m_out.size() - p_position - sizeof(uint32_t));
		m_out[p_position + 0] = static_cast<uint8_t>(size & 0xFF);
		m_out[p_position + 1] = static_cast<uint8_t>((size >> 8) & 0xFF);
		m_out[p_position + 2] = static_cast<uint8_t>((size >> 16) & 0xFF);
		m_out[p_position + 3] = static_cast<uint8_t>((size >> 24) & 0xFF);
	}
}
