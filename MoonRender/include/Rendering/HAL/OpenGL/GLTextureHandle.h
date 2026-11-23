#pragma once
#include <cstdint>
#include <Rendering/HAL/Common/TTextureHandle.h>

namespace Rendering::HAL
{
	struct GLTextureHandleContext
	{
		uint32_t id;
		const uint32_t type;
		/** The handle the object is named by when it is *not* the backend object itself.
		 *
		 * Filament's Texture/HwTexture split: a Texture on the recording side holds a handle, the
		 * GL name lives on the HwTexture in the slot that handle names. Here that means the object
		 * either has `id != 0` (it *is* the backend object, or there is no render thread and the
		 * name was made inline) or `handleId != 0` (it is the front end, and every operation is
		 * recorded as the command that does it). Plain id rather than a typed handle so this
		 * header stays free of the backend and its allocator. */
		uint32_t handleId = 0;
	};

	using GLTextureHandle = TTextureHandle<Settings::EGraphicsBackend::OPENGL, GLTextureHandleContext>;
}
