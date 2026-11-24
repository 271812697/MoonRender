#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include <Rendering/Settings/EBufferType.h>
#include <Rendering/Settings/EAccessSpecifier.h>

namespace Rendering::Backend
{
	/** Asks the driver to create a buffer on the side that owns the context.
	 *
	 * The handle is the front end's: HandleAllocator::Allocate reserved the slot, and the executor
	 * is what gives it a GL name (see Backend::Records::CreateBuffer). Buffers, vertex arrays and
	 * the like are container objects, so unlike textures they are not in the share group - they
	 * have to be born on the render side. */
	struct CreateBufferRequest
	{
		uint32_t handleId = 0;
		Rendering::Settings::EBufferType type = Rendering::Settings::EBufferType::VERTEX;
	};

	/** Asks the driver to bind a buffer to a target, or to one of its indexed binding points.
	 *
	 * A binding point is GL like everything else: with a render thread it has to be set on the side
	 * that owns the context. It used to be issued from the recording side (glBindBufferBase in
	 * GLBuffer::Bind), which left the light SSBO unbound on the executor - every lit surface then
	 * read an empty light list and came out darker than in single-thread mode (docs §8.67). */
	struct BindBufferRequest
	{
		uint32_t handleId = 0;
		Rendering::Settings::EBufferType type = Rendering::Settings::EBufferType::UNKNOWN;
		/** True for an indexed binding point (glBindBufferBase), false for a plain bind. */
		bool indexed = false;
		uint32_t index = 0;
	};

	/** Asks the driver to bind a byte range of a buffer to an indexed binding point
	 * (glBindBufferRange): how a block inside a shared uniform/storage buffer is addressed
	 * (Filament's DescriptorSet::buffer as a buffer + range). */
	struct BindBufferRangeRequest
	{
		uint32_t handleId = 0;
		uint32_t index = 0;
		uint64_t offset = 0;
		uint64_t size = 0;
	};

	/** Asks the driver to allocate storage in an existing buffer. */
	struct AllocateBufferRequest
	{
		uint32_t handleId = 0;
		uint64_t size = 0;
		Rendering::Settings::EAccessSpecifier usage = Rendering::Settings::EAccessSpecifier::STATIC_DRAW;
	};

	/** Asks the driver to copy a range of a buffer into CPU memory, but only once the buffer's
	 * fence says the GPU is done writing it (Filament fetches a PBO the same way).
	 *
	 * The bytes land in p_data, which the request owns, and p_onDone runs where the context is -
	 * with true when the bytes were copied and false when the fence was not signalled yet, so the
	 * recording side can try again on a later frame without ever waiting. */
	struct DownloadBufferRequest
	{
		uint32_t handleId = 0;
		uint64_t offset = 0;
		uint64_t size = 0;
		std::shared_ptr<std::vector<uint8_t>> data;
		std::function<void(bool)> onDone;
	};
}