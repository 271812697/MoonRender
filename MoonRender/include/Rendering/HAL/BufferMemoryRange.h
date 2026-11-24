#pragma once
#include <cstdint>

namespace Rendering::HAL
{
	/**
	* Represents a range of memory in a buffer: the offset/size pair an upload or a range bind is
	* addressed with (Filament's BufferDescriptor offset and size).
	*/
	struct BufferMemoryRange
	{
		uint64_t offset;
		uint64_t size;
	};
}
