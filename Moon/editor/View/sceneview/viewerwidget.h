#pragma once
#include <QOpenGLWidget>
#include <QOpenGLFunctions_4_5_Core>
namespace Core::ECS {
	class Actor;
}
namespace Editor::Panels {
	class AView;
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
		void addActorToTreeView(Core::ECS::Actor* actor);
		void removeActorFromTreeView(Core::ECS::Actor* actor);
		void modifyActorInTreeView(Core::ECS::Actor* actor);
	public slots:
		void onActorSelected(Core::ECS::Actor* actor);
		void onActorHovered(Core::ECS::Actor* actor);
		void onActorHoverLeaved(Core::ECS::Actor* actor);
		void onReadFile(const QString& path);
		void refreshTreeView();
	private:
		class ViewerWindowInternal;
		ViewerWindowInternal* mInternal = nullptr;
	};
}