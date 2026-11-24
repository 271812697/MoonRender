#pragma once

#include <Rendering/Data/Framebuffer.h>

namespace Rendering::Backend
{
	/** The render target a pass draws into, as a command.
	 *
	 * It carries the target, not its GL name: the name is asked for when the command is replayed,
	 * on the render side, so nothing on the recording side has to know one - and a target that was
	 * resized between recording and replay still binds the right object.
	 *
	 * A null target means "the default framebuffer", which is what a pass unbinding its output
	 * means (see Data::Framebuffer::Unbind).
	 */
	struct BindFramebufferRequest
	{
		const ::Rendering::Data::Framebuffer* target = nullptr;
	};
}
