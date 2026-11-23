#pragma once
#include <cstdint>
#include <vector>

#include <Rendering/Settings/EFormat.h>
#include <Rendering/Settings/EPixelDataType.h>

namespace Rendering::Backend
{
	/** A sub-image update into a texture that already has storage (Filament's update2DImage on a
	 * mutable texture).
	 *
	 * Same contract as TextureUploadRequest: the bytes travel with the command, so the recording
	 * side's buffer can be reused immediately and the side that owns the context does the GL call.
	 * Used by the runtime-generated images - the view cube's face atlas is re-uploaded when the
	 * language or theme changes.
	 */
	struct UpdateTextureRequest
	{
		uint32_t handleId = 0;
		uint32_t x = 0;
		uint32_t y = 0;
		uint32_t width = 0;
		uint32_t height = 0;
		Rendering::Settings::EFormat format = Rendering::Settings::EFormat::RGBA;
		Rendering::Settings::EPixelDataType type = Rendering::Settings::EPixelDataType::UNSIGNED_BYTE;
		std::vector<uint8_t> pixels;
	};
}
