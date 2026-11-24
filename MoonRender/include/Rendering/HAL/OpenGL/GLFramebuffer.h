#pragma once
#include <cstdint>
#include <Rendering/HAL/Common/TFramebuffer.h>

namespace Rendering::HAL
{
	/** The GL state of the backend object: the framebuffer's name. The attachments are the GL
	 * framebuffer's own state; the list of them that the recording side reads back stays on
	 * Rendering::Data::Framebuffer. */
	struct GLFramebufferContext
	{
		uint32_t glId = 0;
	};

	using GLFramebuffer = TFramebuffer<Settings::EGraphicsBackend::OPENGL, GLFramebufferContext>;
}
