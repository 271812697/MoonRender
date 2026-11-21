#pragma once
#include <Rendering/Backend/ResourceHandle.h>
#include <Rendering/HAL/Common/TRenderbuffer.h>

namespace Rendering::Backend
{
	struct HwRenderbuffer;
}

namespace Rendering::HAL
{
	struct GLRenderbufferContext
	{
		/** A handle; the GL name is in the record it names (Backend::HwRenderbuffer), on the side
		 * that owns the context. */
		Rendering::Backend::ResourceHandle<Rendering::Backend::HwRenderbuffer> handle;
		uint16_t width = 0;
		uint16_t height = 0;
		Settings::EInternalFormat format = Settings::EInternalFormat::RGBA;
		bool allocated = false;
	};

	using GLRenderbuffer = TRenderbuffer<Settings::EGraphicsBackend::OPENGL, GLRenderbufferContext>;
}
