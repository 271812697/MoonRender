#pragma once
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include <Rendering/Settings/EFormat.h>
#include <Rendering/Settings/EPixelDataType.h>

namespace Rendering::Backend
{
	/** A block of pixels to upload into a texture, owned by the command that carries it (Filament's
	 * PixelBufferDescriptor).
	 *
	 * The same contract as BufferDescriptor: the recording side hands its pixels over here and
	 * forgets about them, because by the time the executor runs they may be gone - a decoded image,
	 * an atlas painted on the fly, a base64 blob read from a package. The format and the pixel type
	 * travel with them, so the update needs nothing else.
	 */
	struct PixelBufferDescriptor
	{
		/** The bytes, and how many of them are valid. Shared so the descriptor copies cheaply
		 * while the command is built (the ring stores arguments by value). */
		std::shared_ptr<const std::vector<uint8_t>> data;
		uint64_t size = 0;
		Rendering::Settings::EFormat format = Rendering::Settings::EFormat::RGBA;
		Rendering::Settings::EPixelDataType type = Rendering::Settings::EPixelDataType::UNSIGNED_BYTE;

		PixelBufferDescriptor() = default;

		/** Copies p_size bytes at p_data. */
		static PixelBufferDescriptor Own(
			const void* p_data,
			uint64_t p_size,
			Rendering::Settings::EFormat p_format,
			Rendering::Settings::EPixelDataType p_type)
		{
			PixelBufferDescriptor descriptor;
			descriptor.format = p_format;
			descriptor.type = p_type;
			if (p_data == nullptr || p_size == 0)
			{
				return descriptor;
			}

			auto bytes = std::make_shared<std::vector<uint8_t>>(static_cast<size_t>(p_size));
			std::memcpy(bytes->data(), p_data, static_cast<size_t>(p_size));
			descriptor.data = std::move(bytes);
			descriptor.size = p_size;
			return descriptor;
		}

		bool IsValid() const { return data != nullptr && size > 0; }
		const uint8_t* Data() const { return data != nullptr ? data->data() : nullptr; }

		Rendering::Settings::EFormat Format() const { return format; }
		Rendering::Settings::EPixelDataType Type() const { return type; }
	};
}
