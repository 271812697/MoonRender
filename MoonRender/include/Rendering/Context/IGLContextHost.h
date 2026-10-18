#pragma once

namespace Rendering::Context
{
	/** The GL context the render module draws on.
	 *
	 * Why this exists: the module used to render on whatever context the host window happened
	 * to leave current - on the Qt side that was the QOpenGLWidget's own context, so whether a
	 * GL call was legal depended on where it was called from, not on who owned the context.
	 * The module now owns its context (created by the window layer, sharing objects with the
	 * window's context) and asks for it here: MakeCurrent before rendering, DoneCurrent after,
	 * and the window's context is current again for whatever it wants to do (its own overlays,
	 * the final blit).
	 *
	 * The interface stays Qt-free on purpose: the window layer implements it with a Qt context,
	 * a GLFW window, or anything else that can make a context current on the calling thread. */
	class IGLContextHost
	{
	public:
		virtual ~IGLContextHost() = default;

		/** Makes the module's context current on the calling thread. Returns false when the
		 * context could not be created or cannot be made current (in which case the caller must
		 * not issue GL calls). */
		virtual bool MakeCurrent() = 0;

		/** Releases the module's context, leaving no context current on this thread - the
		 * window layer then re-binds its own. */
		virtual void DoneCurrent() = 0;

		/** Whether the module's context is the one current on the calling thread. A caller that
		 * binds the context for a whole frame asks this first, so a nested user of the context
		 * does not release it out from under everything that follows. */
		virtual bool IsCurrent() const = 0;
	};
}
