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
		uint32_t framebufferId = 0;
		/** Which colour attachment to read (glReadBuffer(GL_COLOR_ATTACHMENT0 + index)). */
		uint32_t readBufferIndex = 0;
		uint32_t x = 0;
		uint32_t y = 0;
		uint32_t width = 1;
		uint32_t height = 1;
		::Rendering::Settings::EPixelDataFormat format{};
		::Rendering::Settings::EPixelDataType type{};
		/** The bytes land here; one row, tightly packed (the callers read a single pixel). */
		std::shared_ptr<std::vector<uint8_t>> data;
		std::function<void()> onDone;
	};
}
