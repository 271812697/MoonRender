#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <vector>

#include <Rendering/HAL/UniformBuffer.h>

namespace Rendering::Data
{
	/** One GL uniform buffer shared by the std140 blocks the renderer streams: the per-material
	 * parameter blocks and the per-renderable block.
	 *
	 * A block lives at a byte offset inside the buffer and is addressed with glBindBufferRange,
	 * which is the GL form of filament's descriptor "buffer + range": one buffer object instead
	 * of one per material, and a bind that already carries the offset. Slices are persistent - a
	 * material keeps its range until it is destroyed or its block size changes - so a block that
	 * did not change stays valid without being uploaded again.
	 *
	 * Offsets respect GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, which glBindBufferRange requires. The
	 * buffer grows by re-specifying its storage and re-uploading the live region from the CPU
	 * mirror. */
	class UniformBufferPool
	{
	public:
		struct Slice
		{
			uint64_t offset = 0;
			uint32_t size = 0;

			bool IsValid() const { return size > 0; }
		};

		/** The process-wide pool. Leaked on purpose: materials and render features die in an
		 * order the pool cannot know at shutdown, and releasing a slice only touches CPU
		 * bookkeeping. */
		static UniformBufferPool& Get();

		/** Reserves a slice of p_size bytes (aligned) and returns its range. */
		Slice Acquire(uint32_t p_size);
		void Release(Slice p_slice);

		/** Copies p_data into the slice and uploads that range. Needs the GL context current. */
		void Write(Slice p_slice, const void* p_data);

		/** Binds the slice to p_bindingPoint with glBindBufferRange. */
		void Bind(Slice p_slice, uint32_t p_bindingPoint) const;

	private:
		UniformBufferPool() = default;
		UniformBufferPool(const UniformBufferPool&) = delete;
		UniformBufferPool& operator=(const UniformBufferPool&) = delete;

		static uint32_t Alignment();
		void EnsureCapacity(uint64_t p_required);

		std::unique_ptr<Rendering::HAL::UniformBuffer> m_buffer;
		uint64_t m_capacity = 0;
		uint64_t m_usedEnd = 0;
		std::vector<uint8_t> m_data;
		std::map<uint64_t, uint32_t> m_free;
	};
}
