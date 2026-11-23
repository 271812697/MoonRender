#pragma once
#include <Rendering/Backend/ResourceHandle.h>
#include <Rendering/HAL/Common/TBuffer.h>

namespace Rendering::Backend
{
	struct HwBufferObject;
}

namespace Rendering::HAL
{
	struct GLBufferContext
	{
		/** The front-end half: a handle, not a GL name. The name lives in the record this names
		 * (Backend::HwBufferObject, see BackendResources.h), which exists only on the side that
		 * owns the context - Filament's BufferObjectHandle / HwBufferObject split. */
		Rendering::Backend::ResourceHandle<Rendering::Backend::HwBufferObject> handle;
		Settings::EBufferType type = Settings::EBufferType::UNKNOWN;
		uint64_t allocatedBytes = 0;
		/** GLsync handle of the most recent fence, nullptr when none is active. */
		void* fence = nullptr;
	};
	
	using GLBuffer = TBuffer<Settings::EGraphicsBackend::OPENGL, GLBufferContext>;
}
