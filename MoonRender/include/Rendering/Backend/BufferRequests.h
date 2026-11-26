#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace Rendering::Backend
{
	/** Asks the driver to copy a range of a buffer into CPU memory, but only once the buffer's
	 * fence says the GPU is done writing it (Filament fetches a PBO the same way).
	 *
	 * The bytes land in p_data, which the request owns, and p_onDone runs where the context is -
	 * with true when the bytes were copied and false when the fence was not signalled yet, so the
	 * recording side can try again on a later frame without ever waiting.
	 *
	 * The rest of what a buffer can be asked for is the driver API in DriverAPI.inc
	 * (createBufferObject / allocateBufferObject / updateBufferObject / bindBuffer / ...), so those
	 * have no request struct of their own any more. */
	struct DownloadBufferRequest
	{
		uint32_t handleId = 0;
		uint64_t offset = 0;
		uint64_t size = 0;
		std::shared_ptr<std::vector<uint8_t>> data;
		std::function<void(bool)> onDone;
	};
}
