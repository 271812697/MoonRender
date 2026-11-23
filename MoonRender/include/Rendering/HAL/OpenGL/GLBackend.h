#pragma once
#include <thread>

#include <Rendering/HAL/Common/TBackend.h>

namespace Rendering::HAL
{
	struct GLBackendContext
	{
		/** The thread that created the Tracy GPU context (GLBackend::Init). Tracy's GPU context is
		 * thread-local, so collecting from another thread dereferences a null one. */
		std::thread::id initThreadId{};
	};
	using GLBackend = TBackend<Settings::EGraphicsBackend::OPENGL, GLBackendContext>;
}
