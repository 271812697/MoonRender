#pragma once
#include <QOpenGLWidget>
#include <QOpenGLFunctions_4_5_Core>
#include <memory>

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
		/** Pastes a frame the render module produced (its colour texture) into this widget's
		 * framebuffer. Must be called with this widget's context current: the texture is shared
		 * between the two contexts, the framebuffer that reads it is this widget's own. */
		void PasteFrame(uint32_t p_textureID, int p_width, int p_height);
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
	private:
		/** Creates the render module's own GL context (shared with this widget's) and hands it to
		 * the driver. Called from initializeGL right after releasing the widget's context: a
		 * shared context cannot be created while the context it shares from is current. */
		void CreateRenderContext();

		class ViewerWindowInternal;
		ViewerWindowInternal* mInternal = nullptr;
		/** The render module's context; null until CreateRenderContext has succeeded. */
		std::unique_ptr<::Editor::Core::QtGLContextHost> mRenderContextHost;
		/** This widget's own read framebuffer, used to paste the module's frame. */
		unsigned int m_pasteFramebuffer = 0;
	};
}
