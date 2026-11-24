#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include <Rendering/Settings/EPixelDataFormat.h>
#include <Rendering/Settings/EPixelDataType.h>

namespace Rendering::Backend
{
	/** Reads pixels out of a render target on the side that owns the context.
	 *
	 * Filament's readPixels (DriverAPI.inc, OpenGLDriver::readPixels): the destination is owned by
	 * the request and the optional callback runs once the bytes are there, on the thread that did
	 * the read. Picking is what needs it here - the id (and world position) under the cursor.
	 */
	struct ReadPixelsRequest
	{
		/** The target, by handle: resolved where the command runs, so a target created in the same
		 * batch can be read (Filament resolves the target in readPixels too). */
		uint32_t framebufferHandleId = 0;
		/** The same target by GL name, for the callers that already have one. */
		uint32_t framebufferId = 0;
		/** Which colour attachment to read (glReadBuffer(GL_COLOR_ATTACHMENT0 + index)). -1 leaves
		 * the target's current read buffer alone, which is what a recorded
		 * SetFramebufferReadBuffer selects. */
		int32_t readBufferIndex = 0;
		uint32_t x = 0;
		uint32_t y = 0;
		uint32_t width = 1;
		uint32_t height = 1;
		::Rendering::Settings::EPixelDataFormat format{};
		::Rendering::Settings::EPixelDataType type{};
		/** The bytes land here; one row, tightly packed (the callers read a single pixel). */
		std::shared_ptr<std::vector<uint8_t>> data;
		/** Runs where the context is, once the bytes are there. */
		std::function<void()> onDone;
	};

	/** Asks the driver to copy pixels out of a render target into a pixel-pack buffer
	 * (glReadPixels with GL_PIXEL_PACK_BUFFER bound): the GPU writes it, and the bytes are fetched
	 * later, once the buffer's fence is signalled. This is the asynchronous form the HZB readback
	 * uses, so the CPU never waits for the GPU to finish the frame. */
	struct ReadPixelsToBufferRequest
	{
		uint32_t framebufferHandleId = 0;
		uint32_t bufferHandleId = 0;
		uint64_t offset = 0;
		uint32_t x = 0;
		uint32_t y = 0;
		uint32_t width = 1;
		uint32_t height = 1;
		::Rendering::Settings::EPixelDataFormat format{};
		::Rendering::Settings::EPixelDataType type{};
	};
}
