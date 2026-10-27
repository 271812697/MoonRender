#pragma once
#include <memory>

#include <Core/SceneSystem/Scene.h>
#include <Rendering/Data/MaterialInstance.h>
#include <Rendering/Resources/Model.h>
#include <Rendering/View/AViewControllable.h>
#include <Rendering/View/EPreviewMesh.h>

namespace Core::Resources {
	class Material;
}
namespace Core::ECS {
	class Actor;
}

namespace Editor::Panels
{
	/** A view of the preview scene: one mesh, carrying the material the panel hands in.
	 *
	 * It used to be a renderer of its own - a sphere, an engine buffer, one draw call - and what
	 * it showed agreed with the viewport only as long as the material needed nothing the scene
	 * provides. Lighting, the post-process pass and tone mapping are exactly what a material is
	 * judged by, so the preview is a view of a scene now: a small, private one (an actor with a
	 * mesh and a material renderer, a light rig around it, a post-process stack), drawn by the
	 * same SceneRenderer the scene view runs. A difference on screen is then a difference in the
	 * material, not in a second renderer.
	 *
	 * The mesh is the sphere by default; SetPreviewMesh swaps in a cube or a plane for materials
	 * whose UVs or tiling matter more than curvature.
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
		 * context - the renderer, the preview mesh, the scene's actors - so the caller must have
		 * it current. */
		explicit PreviewView(::Rendering::Context::Driver& p_driver);

		/** The material to show on the mesh. Null means "nothing to preview": the material slot
		 * is cleared and no mesh is drawn. The view does not own the material. */
		void SetMaterial(::Core::Resources::Material* p_material);
		::Core::Resources::Material* GetMaterial() const;

		/** Swaps the mesh the material is shown on, keeping the material and the camera. */
		void SetPreviewMesh(EPreviewMesh p_mesh);
		EPreviewMesh GetPreviewMesh() const;

		/** The view's own scene. It is not the editor's: it exists for this view, and nothing
		 * else draws or edits it. */
		::Core::SceneSystem::Scene* GetScene() override;

		/** Moves the camera through the base class, then the light rig - which is laid out
		 * around the sphere in the camera's frame, so orbiting never walks the sphere into the
		 * dark. */
		void Update(float p_deltaTime) override;
		/** Moves the camera back to the preview's default 3/4 pose. */
		void ResetCameraTransform() override;
		/** True while the reset/light-rig animation is still moving the camera. */
		bool IsCameraMoving() const;

	private:
		/** Builds the mesh the preview shows: radius/edge one at the origin, unit normals, a 0..1
		 * UV set and a tangent frame, so a normal map has something to work with. Replaces the
		 * model the actor points at, keeping the actor and the material. */
		void BuildPreviewMesh();
		/** The 1xN colour table the CAD shader samples by domain id. Created once, for the CAD
		 * preview mesh only: without it the domain colour path multiplies the albedo by black. */
		::Rendering::HAL::TextureHandle* DomainColorTable();
		/** Fills the scene: the preview actor, the light rig and the post-process stack. */
		void BuildScene();
		/** Places the rig around the mesh the way the scene view places its own around the
		 * focus (see SceneView::Update), so the material is lit the way the viewport lights it,
		 * whichever side the camera orbits to. */
		void UpdateLightRig();

		/** The mesh, owned here; the actor below points at it. */
		std::unique_ptr<::Rendering::Resources::Model> m_previewModel;
		EPreviewMesh m_previewMesh = EPreviewMesh::SPHERE;
		/** All owned by m_scene. */
		::Core::ECS::Actor* m_previewActor = nullptr;
		::Core::ECS::Actor* m_keyLight = nullptr;
		::Core::ECS::Actor* m_fillLight = nullptr;
		::Core::ECS::Actor* m_rimLightRight = nullptr;
		::Core::ECS::Actor* m_rimLightLeft = nullptr;
		::Core::ECS::Actor* m_headLight = nullptr;
		::Core::Resources::Material* m_material = nullptr;
		/** The instance the preview draws with. An instance, not the asset: the CAD mesh needs a
		 * domain colour table on the material, and that must not be written into the asset the
		 * resource browser and the shapes share. */
		std::unique_ptr<::Rendering::Data::MaterialInstance> m_previewInstance;
		::Rendering::HAL::TextureHandle* m_domainColorTable = nullptr;
		/** Declared last so it is destroyed first: the actors that point at the sphere's meshes
		 * go before the model does. */
		::Core::SceneSystem::Scene m_scene;
	};
}
