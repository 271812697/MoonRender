#pragma once
#include <QOpenGLWidget>
#include <Rendering/View/FramePaster.h>

#include <memory>

namespace MOON {
	class ViewportFrame;

	/** The surface the scene view is shown on.
	 *
	 * Why it is this small: the editor's UI is Qt, so the viewport is a widget, but a widget
	 * is the one thing the render module must not know about (see ViewportFrame). This is
	 * what is left of the window: a GL surface that is laid out and painted by Qt, a size
	 * the frame is told about, the events the frame is handed, and - the only thing it does
	 * on the frame itself - a blit of the module's colour texture into its own framebuffer.
	 *
	 * The frame is rendered on the render module's own context (see RenderModuleHost), and
	 * the blit happens on this widget's context: that is why the two halves are two objects.
	 *
	 * It does not own the module. The module is one per process and everything renders on
	 * it, so it is a singleton (see RenderModuleHost); a widget that showed one would be a
	 * second owner of the editor's only renderer. What this widget does is the one thing
	 * only it can do: give the module its share context when the first GL surface exists,
	 * and take the module down again when that surface goes. */
	class ViewerWidget final : public QOpenGLWidget
	{
		Q_OBJECT
	public:
		explicit ViewerWidget(QWidget* p_parent);
		~ViewerWidget() override;

	protected:
		void initializeGL() override;
		void paintGL() override;
		bool event(QEvent* p_event) override;
		void leaveEvent(QEvent* p_event) override;
		void timerEvent(QTimerEvent* p_event) override;
		void resizeEvent(QResizeEvent* p_event) override;

	private:
		/** Creates the render module - its own context, shared with this widget's, and the
		 * editor Context on it - and the frame that draws on it. Runs with this widget's
		 * context released: a shared context cannot be created while the context it shares
		 * from is current. */
		void CreateRenderModule();

		std::unique_ptr<ViewportFrame> m_frame;
		/** This widget's own read framebuffer, used to paste the module's frame. Created on
		 * this widget's context, so it is released with that context current. */
		Rendering::View::FramePaster m_paster;
	};
}
