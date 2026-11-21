#pragma once
#include <cstdint>
#include <vector>
#include <string>

#include <Rendering/Settings/ETextureType.h>
#include <Rendering/Settings/TextureDesc.h>

namespace Rendering::Backend
{
	/** One texture creation, as the driver receives it.
	 *
	 * Filament's createTexture + TextureDescriptor: the recording side names the texture (by handle
	 * id) and describes it; the side that owns the context allocates the object and, when the
	 * descriptor says so, the storage. Anything recorded after this - an upload, a draw that samples
	 * it - runs later in the ring, so the texture exists by then.
	 */
	struct CreateTextureRequest
	{
		uint32_t handleId = 0;
		Rendering::Settings::ETextureType type = Rendering::Settings::ETextureType::TEXTURE_2D;
		std::string debugName;
		Rendering::Settings::TextureDesc descriptor;
		/** Whether to allocate storage now. Mutable textures are described later, by an upload. */
		bool allocate = false;
		/** Initial contents for a mutable allocation: copied, and pointed at by the descriptor when
		 * the executor allocates. */
		std::vector<uint8_t> pixels;
		bool generateMipmaps = false;
	};
}
