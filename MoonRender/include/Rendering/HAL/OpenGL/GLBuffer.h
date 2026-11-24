#pragma once
#include <Rendering/Backend/BackendResources.h>
#include <Rendering/HAL/Common/TBuffer.h>

namespace Rendering::HAL
{
	/** The GL state of the backend object - the one that lives in the handle's slot. It holds no
	 * handle: this object *is* what the handle names. The kind of buffer and the size of its
	 * storage are in HwBufferObject, TBuffer's base, because those are what a caller holding only
	 * the handle reads; what is left here is what only the side that owns the context can touch. */
	struct GLBufferContext
	{
		/** The GL name, made by the constructor (glCreateBuffers). */
		uint32_t glId = 0;
		/** GLsync handle of the most recent fence, nullptr when none is active. */
		void* fence = nullptr;
	};

	using GLBuffer = TBuffer<Settings::EGraphicsBackend::OPENGL, GLBufferContext>;
}
