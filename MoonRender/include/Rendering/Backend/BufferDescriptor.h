#pragma once
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace Rendering::Backend
{
	/** A block of bytes to upload into a buffer, owned by the command that carries it (Filament's
	 * BufferDescriptor).
	 *
	 * The recording side hands its bytes over here and forgets about them: by the time the executor
	 * runs - in the same batch, on the side that owns the context - the caller's memory may be gone
	 * or rewritten (a mesh's stream built on the stack, the sketch's block colours, a decoded
	 * image). Owning them is what makes the upload independent of the caller's lifetime, the same
	 * reason Filament's PixelBufferDescriptor owns its pixels.
	 */
	struct BufferDescriptor
	{
		/** The bytes, and how many of them are valid. Shared so the descriptor copies cheaply
		 * while the command is built (the ring stores arguments by value). */
		std::shared_ptr<const std::vector<uint8_t>> data;
		uint64_t size = 0;

		BufferDescriptor() = default;

		/** Copies p_size bytes at p_data. */
		static BufferDescriptor Own(const void* p_data, uint64_t p_size)
		{
			BufferDescriptor descriptor;
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
	};
}
