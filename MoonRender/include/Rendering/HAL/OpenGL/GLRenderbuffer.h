#pragma once
#include <cstdint>
#include <Rendering/HAL/Common/TRenderbuffer.h>

namespace Rendering::HAL
{
	/** The GL state of the backend object: the renderbuffer's name. */
	struct GLRenderbufferContext
	{
		uint32_t glId = 0;
	};

	using GLRenderbuffer = TRenderbuffer<Settings::EGraphicsBackend::OPENGL, GLRenderbufferContext>;
}
