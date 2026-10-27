#pragma once
#include <QObject>
#include <string>

namespace Core::SceneSystem { class Scene; }

namespace MOON {
	/** Creates the camera-following light rig (PointLight1..4 + HeadLight) in p_scene if it is
	 * not there yet, and registers it in the tree view. SceneView::Update positions it around the
	 * camera every frame; it is what keeps a body lit from whatever side it is looked at.
	 *
	 * It used to be created only by ParseScene::ParseFile, and only once per process: a document
	 * read by MoonDocument::open (every .moon) never went through that path, so it came up with
	 * the directional light alone and the model looked dark unless a face happened to point at
	 * the sun. Idempotent, so every path that can produce a scene may call it. */
	void EnsureViewportLights(::Core::SceneSystem::Scene* p_scene);

	class ParseScene :public QObject {
		Q_OBJECT
	public:
		ParseScene(QObject* parent);
		void ParseFile(const std::string& path);
	public slots:
		void updateTreeViewSceneRoot();
		void updateTreeViewPathRoot();
	};

}