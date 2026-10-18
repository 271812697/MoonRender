#pragma once
#include <memory>

#include <Core/SceneSystem/Scene.h>
#include <Rendering/Resources/Model.h>
#include <Rendering/View/AViewControllable.h>

namespace Core::Resources {
	class Material;
}
namespace Core::ECS {
	class Actor;
}

namespace Editor::Panels
{
	/** A view of the preview scene: one sphere, carrying the material the panel hands in.
	 *
	 * It used to be a renderer of its own - a sphere, an engine buffer, one draw call - and what
	 * it showed agreed with the viewport only as long as the material needed nothing the scene
	 * provides. Lighting, the post-process pass and tone mapping are exactly what a material is
	 * judged by, so the preview is a view of a scene now: a small, private one (an actor with a
	 * sphere model and a material renderer, a light rig around it, a post-process stack), drawn
	 * by the same SceneRenderer the scene view runs. A difference on screen is then a difference
	 * in the material, not in a second renderer.
	 *
	 * The passes a preview has nothing to feed are switched off in the constructor (see there);
	 * what is left is the scene's own opaque/transparent path and its post-process.
	 *
	 * Size comes from the surface that shows it - Resize(w, h) - and whoever shows it pastes
	 * GetFrameTextureID() into its own framebuffer (see Rendering::View::FramePaster): the
	 * editor's material panel keeps a MaterialPreviewWidget for that. */
	class PreviewView : public AViewControllable
	{
	public:
		/** p_driver is the render module's driver; the view creates its GL resources in that
		 * context - the renderer, the sphere, the scene's actors - so the caller must have it
		 * current. */
		explicit PreviewView(::Rendering::Context::Driver& p_driver);

		/** The material to show on the sphere. Null means "nothing to preview": the material slot
		 * is cleared and no sphere is drawn. The view does not own the material. */
		void SetMaterial(::Core::Resources::Material* p_material);
		::Core::Resources::Material* GetMaterial() const;

		/** The view's own scene. It is not the editor's: it exists for this view, and nothing
		 * else draws or edits it. */
		::Core::SceneSystem::Scene* GetScene() override;

		/** Moves the camera through the base class, then the light rig - which is laid out
		 * around the sphere in the camera's frame, so orbiting never walks the sphere into the
		 * dark. */
		void Update(float p_deltaTime) override;

	private:
		/** Builds the sphere the preview shows: radius one at the origin, unit normals, a 0..1
		 * UV set and a tangent frame, so a normal map has something to work with. */
		void BuildSphere();
		/** Fills the scene: the sphere actor, the light rig and the post-process stack. */
		void BuildScene();
		/** Places the rig around the sphere the way the scene view places its own around the
		 * focus (see SceneView::Update), so the sphere is lit the way the viewport lights it,
		 * whichever side the camera orbits to. */
		void UpdateLightRig();

		/** The sphere, owned here; the actor below points at its mesh. */
		std::unique_ptr<::Rendering::Resources::Model> m_sphere;
		/** All owned by m_scene. */
		::Core::ECS::Actor* m_sphereActor = nullptr;
		::Core::ECS::Actor* m_keyLight = nullptr;
		::Core::ECS::Actor* m_fillLight = nullptr;
		::Core::ECS::Actor* m_rimLightRight = nullptr;
		::Core::ECS::Actor* m_rimLightLeft = nullptr;
		::Core::ECS::Actor* m_headLight = nullptr;
		::Core::Resources::Material* m_material = nullptr;
		/** Declared last so it is destroyed first: the actors that point at the sphere's meshes
		 * go before the model does. */
		::Core::SceneSystem::Scene m_scene;
	};
}
