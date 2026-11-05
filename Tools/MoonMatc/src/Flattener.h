#pragma once
#include <cstdint>
#include <string_view>
#include <vector>

/** Filament's Flattener, in the vector-backed form MoonMatc uses: every write appends the
 * little-endian representation Filament's Unflattener reads. Strings are NUL terminated, blobs
 * are a uint64 length followed by the bytes, and a chunk's size field is patched through the
 * BeginSize/EndSize pair. */
namespace Moon::Matc
{
	class Flattener
	{
	public:
		explicit Flattener(std::vector<uint8_t>& p_out) : m_out(p_out) {}

		void WriteUint8(uint8_t p_value);
		void WriteUint16(uint16_t p_value);
		void WriteUint32(uint32_t p_value);
		void WriteUint64(uint64_t p_value);
		void WriteBool(bool p_value) { WriteUint8(p_value ? 1 : 0); }
		void WriteFloat(float p_value);

		/** Writes the chars and the terminating NUL (filament's writeString). */
		void WriteString(std::string_view p_text);

		/** Writes a uint64 length followed by the raw bytes (filament's writeBlob). */
		void WriteBlob(const void* p_data, size_t p_size);

		/** Reserves the 4-byte chunk size and returns its position. */
		size_t BeginSize();
		/** Patches the size reserved by BeginSize with the bytes written since it. */
		void EndSize(size_t p_position);

		std::vector<uint8_t>& Data() { return m_out; }

	private:
		std::vector<uint8_t>& m_out;
	};
}
