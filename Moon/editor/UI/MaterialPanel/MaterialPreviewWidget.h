#pragma once
#include <QOpenGLWidget>

#include <memory>
#include <string>

#include <Rendering/View/FramePaster.h>

namespace Editor::Panels {
	class PreviewView;
}
namespace Core::Resources {
	class Material;
}

namespace MOON
{
	/** The surface a PreviewView is shown on: a GL widget whose whole job is to paste the view's
	 * frame.
	 *
	 * Why it exists: the editor's UI is Qt, so a view that lives in a panel is shown by a widget.
	 * The rendering still happens on the render module's context, like every other view - this
	 * widget only makes that context current for the duration of the frame, renders its
	 * PreviewView (a scene of its own, see PreviewView), and blits the resulting texture into
	 * its own framebuffer (see Rendering::View::FramePaster). No second context, no readback
	 * through the CPU.
	 *
	 * The shader is created here rather than in the panel: compiling GLSL needs the module's
	 * context current, and that is exactly what this widget has when it paints. The panel only
	 * hands over the package and asks for a repaint. */
	class MaterialPreviewWidget : public QOpenGLWidget
	{
		Q_OBJECT
	public:
		explicit MaterialPreviewWidget(QWidget* p_parent = nullptr);
		~MaterialPreviewWidget() override;

		/** Shows p_package (a material package built by MoonMatc). An empty package leaves the
		 * preview as it is - a compile the panel could not even produce must not blank what is
		 * on screen. The shader is compiled on the next paint, with the module's context
		 * current. */
		void SetShaderPackage(const std::string& p_package, const std::string& p_shaderKey);

		/** The last compile error, empty when the shader was accepted. */
		const std::string& GetError() const;

	signals:
		/** Emitted after a paint in which the shader failed to build, so the panel can log the
		 * driver's message (it carries the .ovfx line numbers). */
		void shaderFailed(const QString& p_error);

	protected:
		void paintGL() override;

		// The preview's camera is driven by drags in this widget: the events are translated into
		// the render module's neutral InputEvent and handed to the view, exactly like the viewport
		// does for the scene view.
		void mousePressEvent(QMouseEvent* p_event) override;
		void mouseMoveEvent(QMouseEvent* p_event) override;
		void mouseReleaseEvent(QMouseEvent* p_event) override;
		void wheelEvent(QWheelEvent* p_event) override;

	private:
		/** Creates the view and its material. Needs the module's context current. */
		bool EnsureView();
		/** Builds the shader for the pending package, if it changed. Needs the context current. */
		void RefreshShader();

		std::string m_package;
		std::string m_shaderKey;
		/** Bumped every time a package arrives, and the generation the shader was built from:
		 * the pair says "this package is already on the GPU", which a key comparison cannot -
		 * editing a file keeps its key but changes its package. */
		uint32_t m_packageGeneration = 0;
		uint32_t m_loadedGeneration = 0;
		std::string m_error;
		/** When the last frame was drawn, for the view's delta time. */
		double m_lastPaintMs = 0.0;

		Editor::Panels::PreviewView* m_view = nullptr;
		/** The material the preview sphere carries. The scene holds a pointer to it, so it
		 * outlives the view (both are released together, view first). */
		std::unique_ptr<::Core::Resources::Material> m_material;
		Rendering::View::FramePaster m_paster;
	};
}
