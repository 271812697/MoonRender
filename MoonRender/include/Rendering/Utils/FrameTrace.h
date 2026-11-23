#pragma once
#include <string_view>

namespace Rendering::Utils::FrameTrace
{
	/** The pass the recording side is in, for the traces the executor prints.
	 *
	 * MOON_FRAME_TRACE writes from two threads: the recording side names the passing (the [pass]
	 * lines), the executor reports the GL it runs ([fbo], [dm]). A bind or a draw does not say
	 * which pass asked for it, so the label travels between them here. The pointer is published
	 * atomically and always names a string with static storage - a pass name in the renderer's map
	 * or a literal - so the executor can read it while the recording side moves on.
	 *
	 * Cold path only: everything here is behind MOON_FRAME_TRACE. */
	void SetPass(const char* p_name);
	const char* GetPass();
}
