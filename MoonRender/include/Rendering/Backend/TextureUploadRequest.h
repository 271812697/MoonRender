#pragma once
#include <cstdint>
#include <optional>
#include <vector>

#include <Rendering/Settings/EFormat.h>
#include <Rendering/Settings/EPixelDataType.h>
#include <Rendering/Settings/TextureDesc.h>

namespace Rendering::Backend
{
	/** One texture upload, as the driver receives it.
	 *
	 * The pixels travel *with* the request: whoever records it hands over a copy, and the side that
	 * owns the context takes it from there. That is Filament's PixelBufferDescriptor role - the
	 * command owns the bytes, so nothing on the recording side can free them before the upload has
	 * run.
	 *
	 * The texture is named by its handle id (a plain integer, not the typed handle), so this header
	 * stays free of the HAL and can be included from the driver itself.
	 */
	struct TextureUploadRequest
	{
		uint32_t handleId = 0;
		Rendering::Settings::TextureDesc descriptor;
		Rendering::Settings::EFormat format = Rendering::Settings::EFormat::RGBA;
		Rendering::Settings::EPixelDataType type = Rendering::Settings::EPixelDataType::UNSIGNED_BYTE;
		std::vector<uint8_t> pixels;
		bool generateMipmaps = false;
	};
}
