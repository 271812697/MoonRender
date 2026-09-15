#pragma once
#include "GizmoBehaviour.h"
#include "AViewControllable.h"
#include "PickingRenderPass.h"
#include "Rendering/Geometry/BoundingSphere.h"

#include <QElapsedTimer>
namespace Core::SceneSystem
{
	class SceneManager;
}
namespace Editor::Panels
{

	class SceneView : public Editor::Panels::AViewControllable
	{
	public:
		SceneView(
			const std::string& p_title
		);

		virtual void Update(float p_deltaTime) override;
		virtual void InitFrame() override;
		virtual ::Core::SceneSystem::Scene* GetScene();
		void FitToSelectedActor(const Maths::FVector3& dir);
		void LookAt(const Maths::FVector3& pivot,const Maths::FVector3& dir,float radius);
		Maths::FVector2 worldToScreen(const Maths::FVector3& worldPos);
		void FitToScene(const Maths::FVector3& dir);
		/** Fits the view to the focus sphere - the selection when there is one,
		 * every active model otherwise - and then applies p_rotation on top of it:
		 * the camera ends up looking at the center of that sphere along the
		 * forward of p_rotation, at the fitted distance, with exactly the rotation
		 * it was given.
		 *
		 * The direction based fits above do the opposite: they derive the
		 * orientation from the direction and the world up. This overload keeps the
		 * orientation, which is what a rotate button needs so its click composes
		 * with the pose the camera is already on.
		 */
		void FitToFocusWithRotation(const Maths::FQuaternion& p_rotation);
		void BuildBvh();
		void SetGizmoOperation(Core::EGizmoOperation p_operation);
		Core::EGizmoOperation GetGizmoOperation() const;
		void ReceiveEvent(QEvent* e);
		bool MouseHit(Maths::FVector3& out);
		bool MouseClipHit(Maths::FVector3& out,const Maths::FVector4& clipPlane);
		Editor::Rendering::PickingRenderPass::PickingResult GetPickResult();
		::Rendering::Geometry::Ray GetMouseRay();
		virtual ::Core::ECS::Actor* GetSelectedActor()override;
		virtual void SelectActor(::Core::ECS::Actor& actor)override;
		virtual void UnselectActor()override;
		virtual bool IsSelectActor()override;
	protected:
		virtual ::Core::Rendering::SceneRenderer::SceneDescriptor CreateSceneDescriptor() override;
	private:
		virtual void DrawFrame() override;
		void HandleActorPicking();
		/** Applies a pick result to the hover / selection state. */
		void ApplyPickResult(const Rendering::PickingRenderPass::PickingResult& p_result);
		/** Synchronous click pick against the picking target drawn this frame. */
		void ResolvePendingClick();
		/** Bounding sphere of the selected model; false when nothing selected has
		 * a model to fit. */
		bool GetSelectionSphere(::Rendering::Geometry::BoundingSphere& p_outSphere);
		/** Bounding sphere of every active model, merged into one. */
		bool GetSceneSphere(::Rendering::Geometry::BoundingSphere& p_outSphere);
		/** The selection when there is one, the scene otherwise. */
		bool GetFocusSphere(::Rendering::Geometry::BoundingSphere& p_outSphere);
		/** Shared fit pose: adjust the projection first, then place the camera
		 * along p_forward at a distance that contains the sphere. */
		void ApplyFitPose(
			::Rendering::Geometry::BoundingSphere& p_sphere,
			const Maths::FVector3& p_forward,
			const Maths::FQuaternion& p_rotation);
	private:
		int64_t mTargetActorId = -1;
		::Core::SceneSystem::SceneManager& m_sceneManager;
		Editor::Core::GizmoBehaviour m_gizmoOperations;
		Editor::Core::EGizmoOperation m_currentOperation = Editor::Core::EGizmoOperation::TRANSLATE;
		::Core::Resources::Material m_fallbackMaterial;
		Tools::Utils::OptRef<::Core::ECS::Actor> m_highlightedActor;
		std::optional<Editor::Core::GizmoBehaviour::EDirection> m_highlightedGizmoDirection;
		Editor::Rendering::PickingRenderPass::PickingResult pickingResult;
		/** Last mouse position the picking readback was performed for. */
		std::pair<double, double> m_lastPickingMouse{ -1.0, -1.0 };
		/** Throttles hover picks: the picking pass is a full scene pass, so it is
		 * rendered on demand at most every kPickThrottleMs milliseconds. */
		QElapsedTimer m_pickRequestTimer;
		/** View projection of the last pick request (refreshes the target while
		 * the camera moves). */
		Maths::FMatrix4 m_lastPickViewProjection = Maths::FMatrix4::Identity;
		bool m_hasLastPickViewProjection = false;
		/** Left click waiting for the synchronous pick on this frame's target. */
		bool m_clickPickPending = false;
		int m_clickPickX = 0;
		int m_clickPickY = 0;
	};
}
