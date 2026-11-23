#pragma once
#include <string>

namespace Core::SceneSystem { class Scene; }

namespace MOON {
	/** Creates the camera-following light rig (PointLight1..4 + HeadLight) in p_scene if it is
	 * not there yet, and registers it in the tree view. SceneView::Update positions it around the
	 * camera every frame; it is what keeps a body lit from whatever side it is looked at.
	 *
	 * It used to be created only by OpenSceneFile, and only once per process: a document
	 * read by MoonDocument::open (every .moon) never went through that path, so it came up with
	 * the directional light alone and the model looked dark unless a face happened to point at
	 * the sun. Idempotent, so every path that can produce a scene may call it. */
	void EnsureViewportLights(::Core::SceneSystem::Scene* p_scene);

	/** Reads p_path into the current scene, by whatever the file is: a model the model
	 * manager can load, a glTF scene, a STEP/BREP body, or one of the engine's own scene
	 * files. Creates the viewport light rig and the post-process stack on the first read.
	 *
	 * It was a QObject with two empty slots that nothing connected to - a free function
	 * wearing an object, and the viewer called it through a pointer that was never set. */
	void OpenSceneFile(const std::string& p_path);

}