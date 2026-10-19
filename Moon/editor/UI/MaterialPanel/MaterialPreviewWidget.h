#pragma once
#include <QImage>
#include <QOpenGLWidget>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <Rendering/View/EPreviewMesh.h>
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

		/** Shows p_material instead of the material built from the panel's package - what the
		 * material designer edits. Passing nullptr goes back to the package material. The
		 * pointer is not owned; it has to outlive the widget's next paint. */
		void ShowMaterial(::Core::Resources::Material* p_material);

		/** What the material is shown on (sphere, cube, plane). The view is created lazily on
		 * the first paint, so the choice is remembered and applied when it exists. */
		void SetPreviewMesh(Editor::Panels::EPreviewMesh p_mesh);

		/** Asks for one frame to be read back and emitted as thumbnailReady, on the next paint.
		 * The browser calls this after a selection and after edits; the readback is throttled
		 * inside the paint, so a slider drag does not read the frame on every event. */
		void RequestThumbnail();

	signals:
		/** Emitted after a paint in which the shader failed to build, so the panel can log the
		 * driver's message (it carries the .mat line numbers). */
		void shaderFailed(const QString& p_error);

		/** The last painted frame, downscaled for the material browser. */
		void thumbnailReady(const QImage& p_image);

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
		/** Points the shown material at the viewport's IBL textures (the sky pass builds them;
		 * TopoShapeActor binds them on the scene's face materials the same way). Needs the
		 * module's context current, like any other material upload. */
		void BindEnvironmentTextures();

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

		/** The thumbnail requested for the next paint, when it was asked for, and when one was
		 * last read back (a drag asks on every event; only one read per burst is needed). */
		bool m_thumbnailRequested = false;
		double m_lastThumbnailMs = 0.0;
		std::vector<uint8_t> m_thumbnailPixels;

		/** What the material is shown on; applied to the view when it is created. */
		Editor::Panels::EPreviewMesh m_previewMesh = Editor::Panels::EPreviewMesh::SPHERE;

		Editor::Panels::PreviewView* m_view = nullptr;
		/** The material the preview sphere carries. The scene holds a pointer to it, so it
		 * outlives the view (both are released together, view first). */
		std::unique_ptr<::Core::Resources::Material> m_material;
		/** The designer's material, when one is being shown; null means m_material. */
		::Core::Resources::Material* m_shownMaterial = nullptr;
		Rendering::View::FramePaster m_paster;
	};
}
