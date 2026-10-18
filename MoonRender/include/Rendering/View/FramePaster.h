#pragma once
#include <cstdint>

namespace Rendering::View
{
	/** Copies a view's frame texture into the framebuffer of whatever context is current.
	 *
	 * Why it is separate from the view: rendering happens on the render module's context, the
	 * image is then shown by a window - a QOpenGLWidget, a widget inside a panel, anything that
	 * has a GL context of its own. That last step is a blit, not rendering: it must run on the
	 * *showing* context, and framebuffer objects belong to a context while textures are shared.
	 * So each surface owns one of these and creates it once its own context is current; the
	 * view knows nothing about it, which is what lets one view be pasted in several places and
	 * several views be pasted side by side.
	 *
	 * The source framebuffer this creates and the destination framebuffer must both be valid in
	 * the context that is current when Paste runs.
	 */
	class FramePaster
	{
	public:
		FramePaster() = default;
		~FramePaster();

		FramePaster(const FramePaster&) = delete;
		FramePaster& operator=(const FramePaster&) = delete;

		/** Blits p_sourceTexture (the view's frame, p_sourceWidth x p_sourceHeight) into
		 * p_destinationFramebuffer (0 = the context's default framebuffer), scaled to
		 * p_destinationWidth x p_destinationHeight.
		 *
		 * p_sourceTexture must be a texture of the current context's share group and a valid GL
		 * texture id: a view that has not rendered yet returns 0, which this ignores. */
		void Paste(uint32_t p_sourceTexture, int p_sourceWidth, int p_sourceHeight,
			uint32_t p_destinationFramebuffer, int p_destinationWidth, int p_destinationHeight);

		/** Deletes the framebuffer now, with the context that created it current. The destructor
		 * calls this; a surface that keeps a paster as a member calls it explicitly while it can
		 * still make its own context current (a Qt widget tearing down, say), because a member's
		 * destructor runs after the surface has had its last chance to bind. Idempotent. */
		void Release();

	private:
		/** Created lazily, in whichever context is current the first time: it is that context's
		 * framebuffer, and it can only be used while that context is current. */
		uint32_t m_readFramebuffer = 0;
	};
}
