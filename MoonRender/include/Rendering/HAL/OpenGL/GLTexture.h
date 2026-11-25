#pragma once
#include <Rendering/HAL/Common/TTexture.h>

namespace Rendering::HAL
{
	/** The GL state of the backend object - the one that lives in the handle's slot. It holds no
	 * handle: this object *is* what the handle names. The kind of texture, the description its
	 * storage was allocated with and the debug name are in HwTexture, TTexture's base, because
	 * those are what a caller holding only the handle reads; what is left here is the GL name. */
	struct GLTextureContext
	{
		uint32_t glId = 0;
	};

	using GLTexture = TTexture<Settings::EGraphicsBackend::OPENGL, GLTextureContext>;
}
