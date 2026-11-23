#include "debugOpenGlWidget.h"
#include "Settings/DebugSetting.h"
namespace MOON {
	static bool isDoRender = false;
	DebugOpenGLWidget::DebugOpenGLWidget(QWidget* parent) {
		this->setFocusPolicy(Qt::StrongFocus);
		this->setMouseTracking(true);
		QSurfaceFormat format;
		format.setSamples(4);
		this->setFormat(format);
	}
	DebugOpenGLWidget::~DebugOpenGLWidget() {

	}
	void DebugOpenGLWidget::initializeGL() {

		QOpenGLWidget::initializeGL();
		// opengl funcs
		bool flag = initializeOpenGLFunctions();

		// 开启计时器（the widget redraws itself; it no longer hosts an ImGui context)
		this->startTimer(0);
	}
	void DebugOpenGLWidget::timerEvent(QTimerEvent* e) {
		this->update();
	}
	void DebugOpenGLWidget::paintGL() {
		if (isDoRender) {
			return;
		}
		isDoRender = true;
		glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
		// Nothing is drawn here until the debug view has a renderer of its own: the module draws
		// its overlays through the command stream now, and the window-side ImGui that used to
		// paint this widget is gone (see docs/RenderThreadPlan.md §8.56).
		isDoRender = false;
	}
	bool DebugOpenGLWidget::event(QEvent* evt) {
		return QOpenGLWidget::event(evt);
	}
	void DebugOpenGLWidget::leaveEvent(QEvent* event) {

	}

	void DebugOpenGLWidget::resizeEvent(QResizeEvent* event) {
		QOpenGLWidget::resizeEvent(event);
	}
	void DebugOpenGLWidget::mousePressEvent(QMouseEvent* event) {

	}

	void DebugOpenGLWidget::mouseMoveEvent(QMouseEvent* event) {

	}

	void DebugOpenGLWidget::mouseReleaseEvent(QMouseEvent* event) {

	}

	void DebugOpenGLWidget::wheelEvent(QWheelEvent* event) {

	}
	void DebugOpenGLWidget::keyPressEvent(QKeyEvent* event) {

	}
	void DebugOpenGLWidget::keyReleaseEvent(QKeyEvent* event) {

	}
	void SetImdockSpace() {
	}
	void DebugOpenGLWidget::showImGui() {

	}
}
