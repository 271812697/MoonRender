#pragma once
#include <cstdint>
#include <memory>
#include <vector>

namespace Rendering::Backend
{
	/** Writes bytes into a buffer on the side that owns the context (Filament's updateBufferObject).
	 *
	 * The bytes are owned by the request, on the heap - Filament's BufferDescriptor does the same -
	 * and not by the command ring: an upload is often far larger than a batch's worth of commands,
	 * and a ring that small must not be used to carry mesh data.
	 */
	struct UpdateBufferRequest
	{
		uint32_t bufferId = 0;
		uint32_t offset = 0;
		uint32_t size = 0;
		std::shared_ptr<const std::vector<uint8_t>> data;
	};

	/** Binds one descriptor of a set: a buffer range to an indexed slot, or a texture to a unit.
	 *
	 * This is the "descriptor set" half of Filament's model: the front end only decides which
	 * resource a binding names, the bind itself is a command, and the draw that follows it sees
	 * the binding because the executor applied it in order.
	 */
	struct BindDescriptorRequest
	{
		enum class Kind : uint8_t
		{
			UniformBuffer,
			ShaderStorageBuffer,
			Texture,
		};

		Kind kind = Kind::UniformBuffer;
		/** The GL name of the buffer or texture the descriptor names. */
		uint32_t objectId = 0;
		/** The indexed slot (buffer) or texture unit (texture), as the program's binding map says. */
		uint32_t slot = 0;
		uint32_t offset = 0;
		uint32_t size = 0;
	};
}
