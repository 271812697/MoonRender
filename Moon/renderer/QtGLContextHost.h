#pragma once
#include <memory>
#include <Rendering/Context/IGLContextHost.h>

class QOpenGLContext;
class QOffscreenSurface;

namespace Editor::Core
{
	/** The render module's GL context, on the Qt side.
	 *
	 * It is a context of its own, sharing objects with the viewport's QOpenGLWidget context:
	 * sharing is what lets the module draw with the shaders, textures and buffers the rest of
	 * the editor created, and what lets the viewport sample the frame the module rendered. It
	 * still is a separate context, so the module never again renders on whatever context Qt
	 * happened to leave current.
	 *
	 * Creation has one hard constraint, the same one a shared context always has on Windows:
	 * wglShareLists fails with ERROR_BUSY while the context being shared from is current, so
	 * this must be constructed when the widget's context is NOT current (see
	 * ViewerWidget::CreateRenderContext, which defers it to the event loop for that reason). */
	class QtGLContextHost final : public ::Rendering::Context::IGLContextHost
	{
	public:
		/** p_shareContext is the context whose objects the module will use (the viewport's). */
		explicit QtGLContextHost(QOpenGLContext* p_shareContext);
		~QtGLContextHost() override;

		QtGLContextHost(const QtGLContextHost&) = delete;
		QtGLContextHost& operator=(const QtGLContextHost&) = delete;

		/** True once the context and its surface exist: only then is making it current legal. */
		bool IsValid() const;

		bool MakeCurrent() override;
		void DoneCurrent() override;
		bool IsCurrent() const override;

	private:
		std::unique_ptr<QOpenGLContext> m_context;
		std::unique_ptr<QOffscreenSurface> m_surface;
	};
}
