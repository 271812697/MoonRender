#pragma once
#include <QOpenGLWidget>
#include <QOpenGLFunctions_4_5_Core>
#include <memory>
#include <string>
#include <QImage>

namespace Rendering::Data {
	class Material;
}
namespace Rendering::Resources {
	class Shader;
}
namespace Core::Rendering {
	class MaterialPreviewRenderer;
}
namespace Core::ECS {
	class Actor;
}
namespace Editor::Panels {
	class AView;
}
namespace Editor::Core {
	class QtGLContextHost;
}
namespace MOON {
	class ViewerWidget : public QOpenGLWidget, QOpenGLFunctions_4_5_Core
	{
		Q_OBJECT
	public:
		explicit ViewerWidget(QWidget* parent);
		~ViewerWidget();
		void initializeGL() override;
		void timerEvent(QTimerEvent* e) override;
		void paintGL() override;
		bool event(QEvent* evt) override;
		void leaveEvent(QEvent* event) override;
		void resizeEvent(QResizeEvent* event) override;
		void mousePressEvent(QMouseEvent* event) override;
		void mouseMoveEvent(QMouseEvent* event) override;
		void mouseReleaseEvent(QMouseEvent* event) override;
		void wheelEvent(QWheelEvent* event) override;
		void keyPressEvent(QKeyEvent* event) override;
		void keyReleaseEvent(QKeyEvent* event) override;
		::Editor::Panels::AView* getView();
		/** The file the scene was last read from, empty while nothing was opened: the
		 * project the start page shows a card for. */
		QString getDocumentPath() const;
		/** Forgets that file: the document that was open is closed, and what it was read
		 * from is not what is open any more. */
		void clearDocumentPath();
		void addActorToTreeView(Core::ECS::Actor* actor);
		void removeActorFromTreeView(Core::ECS::Actor* actor);
		void modifyActorInTreeView(Core::ECS::Actor* actor);
		/** Asks for a preview of the shader the archive holds under p_key, on a sphere.
		 *
		 * The panel only leaves a request: the render happens in this widget's own frame, where
		 * the context is current and the render module is in a known state, and comes back
		 * through materialPreviewReady. Doing it from the caller's callback - a timer, a button -
		 * means making a context current behind the render module's back, which is what used to
		 * take the process down in release builds. */
		void RequestMaterialPreview(const std::string& p_key, int p_size);
		/** Compiles a shader package (the bytes MoonMatcLib produced) into a shader, with this
		 * widget's GL context current - compiling GLSL from an editor callback that has no
		 * context current does not work. Returns nullptr and fills p_error when the driver
		 * refused it; the message carries the .ovfx lines. */
		/** True once the widget has a GL context (i.e. the viewport has been shown at least
		 * once). The material editor waits for it before asking for a preview. */
		bool HasGLContext() const;
		/** Pastes a frame the render module produced (its colour texture) into this widget's
		 * framebuffer. Must be called with this widget's context current: the texture is shared
		 * between the two contexts, the framebuffer that reads it is this widget's own. */
		void PasteFrame(uint32_t p_textureID, int p_width, int p_height);
		::Rendering::Resources::Shader* CreateShaderFromPackage(const std::string& p_package,
			const std::string& p_name, std::string& p_error);
	public slots:
		void onActorSelected(Core::ECS::Actor* actor);
		void onActorHovered(Core::ECS::Actor* actor);
		void onActorHoverLeaved(Core::ECS::Actor* actor);
		void onReadFile(const QString& path);
		void refreshTreeView();
	signals:
		/** A document was read into the scene - a .moon document rebuilt, or a model
		 * imported. p_path is the file it came from: what keeps the project in the
		 * recent list is told here, in the render loop, where the read actually
		 * happens (asking for one only leaves the request behind). */
		void documentOpened(const QString& p_path);
		/** A material preview was rendered, in the render loop. Empty when it failed. */
		void materialPreviewReady(const QImage& p_image);
	private:
		/** Creates the render module's own GL context (shared with this widget's) and hands it to
		 * the driver. Called from initializeGL right after releasing the widget's context: a
		 * shared context cannot be created while the context it shares from is current. */
		void CreateRenderContext();
		/** Renders the pending preview request, if any. Runs inside this widget's frame, with the
		 * context current, so the panel never touches GL itself. */
		void RenderPendingPreview();

		class ViewerWindowInternal;
		ViewerWindowInternal* mInternal = nullptr;
		/** Built on the first preview, once the context exists. */
		std::unique_ptr<::Core::Rendering::MaterialPreviewRenderer> mPreviewRenderer;
		/** The render module's context; null until CreateRenderContext has succeeded. */
		std::unique_ptr<::Editor::Core::QtGLContextHost> mRenderContextHost;
		/** This widget's own read framebuffer, used to paste the module's frame. */
		unsigned int m_pasteFramebuffer = 0;
		/** The preview the material editor asked for, and has not been rendered yet. */
		std::string mPreviewRequestKey;
		int mPreviewRequestSize = 0;
	};
}
