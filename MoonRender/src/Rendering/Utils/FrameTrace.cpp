#include <atomic>

#include <Rendering/Utils/FrameTrace.h>

namespace
{
	std::atomic<const char*> g_currentPass{ "" };
}

namespace Rendering::Utils::FrameTrace
{
	void SetPass(const char* p_name)
	{
		g_currentPass.store(p_name != nullptr ? p_name : "", std::memory_order_release);
	}

	const char* GetPass()
	{
		return g_currentPass.load(std::memory_order_acquire);
	}
}
