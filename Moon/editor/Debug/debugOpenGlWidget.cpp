#include "debugOpenGlWidget.h"
#include "Settings/DebugSetting.h"
#include "Qtimgui/imguiwidgets/QtImGui.h"
#include <ThirdParty/imgui/imgui/imgui.h>
#include <ThirdParty/imgui/imgui/imgui_internal.h>
#include <ThirdParty/imgui/implot/implotCustom.h>
#include <ThirdParty/imgui/implot/imGuizmo.h>
namespace MOON {
	static QtImGui::RenderRef imref = nullptr;
	static ImPlotContext* ctx = nullptr;
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

		//寮€鍚鏃跺櫒
		this->startTimer(0);
		imref = QtImGui::initialize(this, false);
		ctx = ImPlot::CreateContext();
	}
	void DebugOpenGLWidget::timerEvent(QTimerEvent* e) {
		this->update();
	}
	void DebugOpenGLWidget::paintGL() {
		if (isDoRender) {
			return;
		}
		isDoRender = true;
		QtImGui::newFrame(imref);
		ImPlot::SetCurrentContext(ctx);
		glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
		showImGui();
		ImGui::Render();
		QtImGui::render(imref);
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
		//ImGuiViewport* viewport = ImGui::GetMainViewport();
		//ImGui::SetNextWindowPos(viewport->Pos);
		//ImGui::SetNextWindowSize(viewport->Size);
		//ImGui::SetNextWindowViewport(viewport->ID);
		//ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		//ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		//ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

		//ImGui::Begin("##dockspace", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking);
		//ImGuiID dockspace_id = ImGui::GetID("MyDockspace");
		//ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode | ImGuiDockNodeFlags_NoWindowMenuButton);
		//ImGui::SetWindowPos({ 0.f, 0.f });
		//ImVec2 displaySize = ImGui::GetIO().DisplaySize;
		//ImGui::SetWindowSize({ (float)displaySize.x, (float)displaySize.y });
		//ImGui::End();

		//ImGui::PopStyleVar(3);
	}
	void DebugOpenGLWidget::showImGui() {

	}
}



