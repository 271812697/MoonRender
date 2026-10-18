#pragma once

#include <QOpenGLExtraFunctions>
#include <QObject>
#include <QPoint>
#include <ThirdParty/imgui/imgui/imgui.h>
#include <Rendering/ImGui/ImGuiGLRenderer.h>
#include <memory>

class QMouseEvent;
class QWheelEvent;
class QKeyEvent;

namespace QtImGui {

class WindowWrapper {
public:
    virtual ~WindowWrapper() {}
    virtual void installEventFilter(QObject *object) = 0;
    virtual QSize size() const = 0;
    virtual qreal devicePixelRatio() const = 0;
    virtual bool isActive() const = 0;
    virtual QPoint mapFromGlobal(const QPoint &p) const = 0;
    virtual QObject* object() = 0;
    
    virtual void setCursorShape(Qt::CursorShape shape) = 0;
    virtual void setCursorPos(const QPoint& local_pos) = 0;
};

class ImGuiRenderer : public QObject, QOpenGLExtraFunctions {
    Q_OBJECT
public:
    void initialize(WindowWrapper *window);
    void newFrame();
    void render();
    bool eventFilter(QObject *watched, QEvent *event);

    static ImGuiRenderer *instance();

public:
    ImGuiRenderer();
    ~ImGuiRenderer();

private:
    void onMousePressedChange(QMouseEvent *event);
    void onWheel(QWheelEvent *event);
    void onKeyPressRelease(QKeyEvent *event);
    
    void updateCursorShape(const ImGuiIO &io);
    void setCursorPos(const ImGuiIO &io);

    std::unique_ptr<WindowWrapper> m_window;
    double       g_Time = 0.0f;
    bool         g_MousePressed[3] = { false, false, false };
    float        g_MouseWheel;
    float        g_MouseWheelH;

    ImGuiContext* g_ctx = nullptr;
    /** Draws the draw data: the shader, buffers and font texture are the render module's - see
     * Rendering::ImGui::ImGuiGLRenderer. This class is only the window's half of ImGui: input,
     * clipboard, cursor shape, display size. */
    ::Rendering::ImGui::ImGuiGLRenderer m_glRenderer;
};

} // namespace QtImGui
