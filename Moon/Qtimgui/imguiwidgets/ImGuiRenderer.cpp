#include "ImGuiRenderer.h"
#include <ThirdParty/imgui/imgui/imgui_freetype.h>
#include <ThirdParty/imgui/implot/implot.h>
#include <ThirdParty/imgui/implot/implotCustom.h>

#include <QDateTime>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QClipboard>
#include <QCursor>
#include <Tools/Utils/PathParser.h>
#ifdef ANDROID
#define GL_VERTEX_ARRAY_BINDING           0x85B5 // Missing in android as of May 2020
#define USE_GLSL_ES
#endif

#ifdef USE_GLSL_ES
#define IMGUIRENDERER_GLSL_VERSION "#version 300 es\n"
#else
#define IMGUIRENDERER_GLSL_VERSION "#version 330\n"
#endif

namespace QtImGui {

	namespace {

		// Keyboard mapping. Dear ImGui use those indices to peek into the io.KeysDown[] array.
		const QHash<int, ImGuiKey> keyMap =
		{
			{ Qt::Key_Tab, ImGuiKey_Tab },
			{ Qt::Key_Left, ImGuiKey_LeftArrow },
			{ Qt::Key_Right, ImGuiKey_RightArrow },
			{ Qt::Key_Up, ImGuiKey_UpArrow },
			{ Qt::Key_Down, ImGuiKey_DownArrow },
			{ Qt::Key_Up, ImGuiKey_UpArrow },
			{ Qt::Key_Down, ImGuiKey_DownArrow },
			{ Qt::Key_PageUp, ImGuiKey_PageUp },
			{ Qt::Key_PageDown, ImGuiKey_PageDown },
			{ Qt::Key_Home, ImGuiKey_Home },
			{ Qt::Key_End, ImGuiKey_End },
			{ Qt::Key_Insert, ImGuiKey_Insert },
			{ Qt::Key_Delete, ImGuiKey_Delete },
			{ Qt::Key_Backspace, ImGuiKey_Backspace },
			{ Qt::Key_Space, ImGuiKey_Space },
			{ Qt::Key_Enter, ImGuiKey_Enter },
			{ Qt::Key_Return, ImGuiKey_Enter },
			{ Qt::Key_Escape, ImGuiKey_Escape },
			{ Qt::Key_A, ImGuiKey_A },
			{ Qt::Key_C, ImGuiKey_C },
			{ Qt::Key_V, ImGuiKey_V },
			{ Qt::Key_X, ImGuiKey_X },
			{ Qt::Key_Y, ImGuiKey_Y },
			{ Qt::Key_Z, ImGuiKey_Z }
		};

#ifndef QT_NO_CURSOR
		const QHash<ImGuiMouseCursor, Qt::CursorShape> cursorMap = {
			{ ImGuiMouseCursor_Arrow,      Qt::CursorShape::ArrowCursor },
			{ ImGuiMouseCursor_TextInput,  Qt::CursorShape::IBeamCursor },
			{ ImGuiMouseCursor_ResizeAll,  Qt::CursorShape::SizeAllCursor },
			{ ImGuiMouseCursor_ResizeNS,   Qt::CursorShape::SizeVerCursor },
			{ ImGuiMouseCursor_ResizeEW,   Qt::CursorShape::SizeHorCursor },
			{ ImGuiMouseCursor_ResizeNESW, Qt::CursorShape::SizeBDiagCursor },
			{ ImGuiMouseCursor_ResizeNWSE, Qt::CursorShape::SizeFDiagCursor },
			{ ImGuiMouseCursor_Hand,       Qt::CursorShape::PointingHandCursor },
			{ ImGuiMouseCursor_NotAllowed, Qt::CursorShape::ForbiddenCursor },
		};
#endif

		QByteArray g_currentClipboardText;

	} // namespace

	void ImGuiRenderer::initialize(WindowWrapper* window) {
		m_window.reset(window);
		initializeOpenGLFunctions();

		g_ctx = ImGui::CreateContext();
		ImPlot::CreateContext();
		ImGui::SetCurrentContext(g_ctx);

		// Setup backend capabilities flags
		ImGuiIO& io = ImGui::GetIO();
		//io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
#ifndef QT_NO_CURSOR
		io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors; // We can honor GetMouseCursor() values (optional)
		io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;  // We can honor io.WantSetMousePos requests (optional, rarely used)
#endif
		io.BackendPlatformName = "qtimgui";


		// Setup keyboard mapping
		for (ImGuiKey key : keyMap.values()) {
			//io.KeysData[key] = key;
			//io.KeyMap[key] = key;
		}

		//io.RenderDrawListsFn = [](ImDrawData *drawData) {
	   //    instance()->renderDrawList(drawData);
	   // };
		io.SetClipboardTextFn = [](void* user_data, const char* text) {
			Q_UNUSED(user_data);
			QGuiApplication::clipboard()->setText(text);
			};
		io.GetClipboardTextFn = [](void* user_data) {
			Q_UNUSED(user_data);
			g_currentClipboardText = QGuiApplication::clipboard()->text().toUtf8();
			return (const char*)g_currentClipboardText.data();
			};

		window->installEventFilter(this);

		// The drawing half: shader, buffers and font texture, created on whatever context is
		// current here (the render module's) - see Rendering::ImGui::ImGuiGLRenderer.
		m_glRenderer.Initialize();
	}

	void ImGuiRenderer::newFrame()
	{
		ImGui::SetCurrentContext(g_ctx);

		ImGuiIO& io = ImGui::GetIO();

		// Setup display size (every frame to accommodate for window resizing)
		io.DisplaySize = ImVec2(m_window->size().width(), m_window->size().height());
		io.DisplayFramebufferScale = ImVec2(m_window->devicePixelRatio(), m_window->devicePixelRatio());

		// Setup time step
		double current_time = QDateTime::currentMSecsSinceEpoch() / double(1000);
		io.DeltaTime = g_Time > 0.0 ? (float)(current_time - g_Time) : (float)(1.0f / 60.0f);
		if (io.DeltaTime <= 0.0f) io.DeltaTime = 0.00001f;
		g_Time = current_time;

		// If ImGui wants to set the cursor position (keyboard navigation), do it before reading it.
		setCursorPos(io);

		if (m_window->isActive())
		{
			const QPoint pos = m_window->mapFromGlobal(QCursor::pos());
			io.MousePos = ImVec2(pos.x(), pos.y());
		}
		else
		{
			io.MousePos = ImVec2(-1, -1);
		}

		for (int i = 0; i < 3; i++)
		{
			io.MouseDown[i] = g_MousePressed[i];
		}

		io.MouseWheelH = g_MouseWheelH;
		io.MouseWheel = g_MouseWheel;
		g_MouseWheelH = 0;
		g_MouseWheel = 0;

		updateCursorShape(io);

		ImGui::NewFrame();
		ImPlotCustom::testImPlotCustom();
	}

	void ImGuiRenderer::render()
	{
		ImGui::SetCurrentContext(g_ctx);

		ImDrawData* drawData = ImGui::GetDrawData();
		if (drawData == nullptr)
		{
			return;
		}

		// The framebuffer can be bigger than the logical size ImGui laid out in (high-DPI).
		const ImGuiIO& io = ImGui::GetIO();
		const int framebufferWidth = static_cast<int>(io.DisplaySize.x * io.DisplayFramebufferScale.x);
		const int framebufferHeight = static_cast<int>(io.DisplaySize.y * io.DisplayFramebufferScale.y);
		m_glRenderer.Render(drawData, framebufferWidth, framebufferHeight);
	}

	ImGuiRenderer::ImGuiRenderer()
		: g_ctx(nullptr)
	{
	}

	ImGuiRenderer::~ImGuiRenderer()
	{
		// remove this context
		ImPlot::DestroyContext();
		ImGui::DestroyContext(g_ctx);
		
	}

	void ImGuiRenderer::onMousePressedChange(QMouseEvent* event)
	{
		g_MousePressed[0] = event->buttons() & Qt::LeftButton;
		g_MousePressed[1] = event->buttons() & Qt::RightButton;
		g_MousePressed[2] = event->buttons() & Qt::MiddleButton;
	}

	void ImGuiRenderer::onWheel(QWheelEvent* event)
	{
		// Select current context
		ImGui::SetCurrentContext(g_ctx);

		// Handle horizontal component
		if (event->pixelDelta().x() != 0)
		{
			g_MouseWheelH += event->pixelDelta().x() / (ImGui::GetTextLineHeight());
		}
		else {
			// Magic number of 120 comes from Qt doc on QWheelEvent::pixelDelta()
			g_MouseWheelH += event->angleDelta().x() / 120.0f;
		}

		// Handle vertical component
		if (event->pixelDelta().y() != 0)
		{
			// 5 lines per unit
			g_MouseWheel += event->pixelDelta().y() / (5.0 * ImGui::GetTextLineHeight());
		}
		else {
			// Magic number of 120 comes from Qt doc on QWheelEvent::pixelDelta()
			g_MouseWheel += event->angleDelta().y() / 120.0f;
		}
	}

	void ImGuiRenderer::onKeyPressRelease(QKeyEvent* event)
	{
		// Select current context
		ImGui::SetCurrentContext(g_ctx);

		ImGuiIO& io = ImGui::GetIO();

		const bool key_pressed = (event->type() == QEvent::KeyPress);

		// Translate `Qt::Key` into `ImGuiKey`, and apply 'pressed' state for that key
		const auto key_it = keyMap.constFind(event->key());
		if (key_it != keyMap.constEnd()) { // Qt's key found in keyMap
			const int imgui_key = *(key_it);
			io.AddKeyEvent(static_cast<ImGuiKey>(imgui_key), key_pressed);
			//io.KeysDown[imgui_key] = key_pressed;
		}

		if (key_pressed) {
			const QString text = event->text();
			if (text.size() == 1) {
				io.AddInputCharacter(text.at(0).unicode());
			}
		}

#ifdef Q_OS_MAC
		io.KeyCtrl = event->modifiers() & Qt::MetaModifier;
		io.KeyShift = event->modifiers() & Qt::ShiftModifier;
		io.KeyAlt = event->modifiers() & Qt::AltModifier;
		io.KeySuper = event->modifiers() & Qt::ControlModifier; // Comamnd key
#else
		io.KeyCtrl = event->modifiers() & Qt::ControlModifier;
		io.KeyShift = event->modifiers() & Qt::ShiftModifier;
		io.KeyAlt = event->modifiers() & Qt::AltModifier;
		io.KeySuper = event->modifiers() & Qt::MetaModifier;
#endif
	}

	void ImGuiRenderer::updateCursorShape(const ImGuiIO& io)
	{
		// NOTE: This code will be executed, only if the following flags have been set:
		// - backend flag: `ImGuiBackendFlags_HasMouseCursors`    - enabled
		// - config  flag: `ImGuiConfigFlags_NoMouseCursorChange` - disabled

#ifndef QT_NO_CURSOR
		if (io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange)
			return;

		const ImGuiMouseCursor imgui_cursor = ImGui::GetMouseCursor();
		if (io.MouseDrawCursor || (imgui_cursor == ImGuiMouseCursor_None))
		{
			// Hide OS mouse cursor if imgui is drawing it or if it wants no cursor
			m_window->setCursorShape(Qt::CursorShape::BlankCursor);
		}
		else
		{
			// Show OS mouse cursor

			// Translate `ImGuiMouseCursor` into `Qt::CursorShape` and show it, if we can
			const auto cursor_it = cursorMap.constFind(imgui_cursor);
			if (cursor_it != cursorMap.constEnd()) // `Qt::CursorShape` found for `ImGuiMouseCursor`
			{
				const Qt::CursorShape qt_cursor_shape = *(cursor_it);
				m_window->setCursorShape(qt_cursor_shape);
			}
			else // shape NOT found - use default
			{
				m_window->setCursorShape(Qt::CursorShape::ArrowCursor);
			}
		}
#else
		Q_UNUSED(io);
#endif
		}

	void ImGuiRenderer::setCursorPos(const ImGuiIO& io)
	{
		// NOTE: This code will be executed, only if the following flags have been set:
		// - backend flag: `ImGuiBackendFlags_HasSetMousePos`      - enabled
		// - config  flag: `ImGuiConfigFlags_NavEnableSetMousePos` - enabled

#ifndef QT_NO_CURSOR
		if (io.WantSetMousePos) {
			m_window->setCursorPos({ (int)io.MousePos.x, (int)io.MousePos.y });
		}
#else
		Q_UNUSED(io);
#endif
		}

	bool ImGuiRenderer::eventFilter(QObject* watched, QEvent* event)
	{
		if (watched == m_window->object()) {
			switch (event->type()) {
			case QEvent::MouseButtonDblClick:
			case QEvent::MouseButtonPress:
			case QEvent::MouseButtonRelease:
				this->onMousePressedChange(static_cast<QMouseEvent*>(event));
				break;
			case QEvent::Wheel:
				this->onWheel(static_cast<QWheelEvent*>(event));
				break;
			case QEvent::KeyPress:
			case QEvent::KeyRelease:
				this->onKeyPressRelease(static_cast<QKeyEvent*>(event));
				break;
			default:
				break;
			}
		}
		return QObject::eventFilter(watched, event);
	}

	ImGuiRenderer* ImGuiRenderer::instance() {
		static ImGuiRenderer* instance = nullptr;
		if (!instance) {
			instance = new ImGuiRenderer();
		}
		return instance;
	}

	} // namespace QtImGui
