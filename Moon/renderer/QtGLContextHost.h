#pragma once
#include <memory>
#include <Rendering/Context/IGLContextHost.h>
#include <Rendering/Context/IPlatform.h>

class QOpenGLContext;
class QOffscreenSurface;

namespace Editor::Core
{
	/** The render module's GL context, on the Qt side.
	 *
	 * It is a context of its own, in the same share group as the editor's windows: sharing is
	 * what lets the module draw with the shaders, textures and buffers the rest of the editor
	 * created, and what lets a widget sample the frame the module rendered. It still is a
	 * separate context, so the module never again renders on whatever context Qt happened to
	 * leave current.
	 *
	 * Creation has one hard constraint, the same one a shared context always has on Windows:
	 * wglShareLists fails with ERROR_BUSY while the context being shared from is current, so
	 * this must be constructed when the widget's context is NOT current (see
	 * ViewerWidget::CreateRenderContext, which defers it to the event loop for that reason). */
	class QtGLContextHost final : public ::Rendering::Context::IGLContextHost, public ::Rendering::Context::IPlatform
	{
	public:
		/** p_shareContext is the context whose share group the module joins: the viewport's, or
		 * Qt's global share context when the application has one (see
		 * ViewerWidget::CreateRenderContext). */
		explicit QtGLContextHost(QOpenGLContext* p_shareContext);

		/** Without a shared context: the platform entry point creates the Qt context later, on the
		 * thread that calls CreateDriver (see IPlatform). */
		QtGLContextHost();
		~QtGLContextHost() override;

		QtGLContextHost(const QtGLContextHost&) = delete;
		QtGLContextHost& operator=(const QtGLContextHost&) = delete;

		/** True once the context and its surface exist: only then is making it current legal. */
		bool IsValid() const;

		/** The Qt context. Used when a second context has to share objects with this one - which
		 * is what a render thread would need, since Qt pins a context to the thread that created
		 * it (see docs/RenderThreadPlan.md). Null before it is created. */
		QOpenGLContext* Context() const;

		/** Creates the offscreen surface the module's context will be made current on.
		 *
		 * Qt requires a QOffscreenSurface to be created on the GUI thread; the context itself may be
		 * created on the render thread. So the host prepared here - on the widget's thread, before
		 * the engine starts its own - and CreateDriver only creates the context and binds it. */
		bool PrepareSurface(QOpenGLContext* p_shareContext);

		/** Creates the driver - and, the first time, the Qt context and its surface - on the
		 * calling thread (Filament's Platform::createDriver). The engine calls this from its
		 * render thread, so every GL object the frame uses is created there. */
		::Rendering::Context::Driver* CreateDriver(void* p_sharedContext) override;
		void DestroyDriver(::Rendering::Context::Driver* p_driver) override;

		bool MakeCurrent() override;
		void DoneCurrent() override;
		bool IsCurrent() const override;

	private:
		std::unique_ptr<QOpenGLContext> m_context;
		std::unique_ptr<QOffscreenSurface> m_surface;
	};
}
