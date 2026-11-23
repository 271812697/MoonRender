#pragma once
#include "GizmoBehaviour.h"
#include <Rendering/View/AViewControllable.h>
#include "PickingRenderPass.h"
#include "Rendering/Geometry/BoundingSphere.h"

#include <chrono>
#include <functional>
#include <optional>

#include <Tools/Eventing/Event.h>
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
		~SceneView();

		virtual void Update(float p_deltaTime) override;
		virtual void InitFrame() override;
		virtual ::Core::SceneSystem::Scene* GetScene();
		void FitToSelectedActor(const Maths::FVector3& dir);
		/** Fits whatever the view is about to work on - the selection, or the whole
		 * scene when there is no selection or it cannot be framed - and looks from
		 * p_dir. Used by the view cube faces, which must do something on every click. */
		void FitToFocus(const Maths::FVector3& dir) override;
		void LookAt(const Maths::FVector3& pivot,const Maths::FVector3& dir,float radius);
		Maths::FVector2 worldToScreen(const Maths::FVector3& worldPos);
		/** Bounding sphere of every active model, merged into one. */
		bool GetSceneSphere(::Rendering::Geometry::BoundingSphere& p_outSphere);
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
		/** Frames what the view is about to work on - and builds the scene query that the
		 * fit and every culling decision read - as soon as the scene has something to
		 * frame.
		 *
		 * A document that was just read arrives without scene query structures and with the
		 * camera still on the previous one. Fitting it right away is wrong for the reads
		 * that fill their actors on a job thread (a STEP import): the actors are in the
		 * scene while the models they will hold are still empty, and framing those frames
		 * the previous document. So the fit is a request: it is ticked once per Update, and
		 * waits for the first frame in which a model actually has bounds. A file that was
		 * never readable gives up after kFitTimeoutFrames frames, so a request cannot fire
		 * later, on geometry an unrelated feature added.
		 *
		 * p_onFitted runs right after the fit, in the frame it happened (the viewport cuts
		 * a demo section there).
		 *
		 * An empty scene query is worse than none: the camera controller reads the scene
		 * bounds to scale its zoom and pan, and the default (empty) box gives it infinities
		 * to scale by. That is why the build waits for a triangle - unless the wait has run
		 * out, so a triangle-free document is still framed. */
		void RequestFitWhenReady(std::function<void()> p_onFitted = {});
		/** True while a RequestFitWhenReady has not been served or given up on. */
		bool IsFitPending() const;
		/** How many frames the pending fit has left before it gives up. */
		int PendingFitFrames() const;
		/** True during the first frames of a pending fit: what the frame loop traces, so a
		 * crash a few frames after a read says on which step it happened. */
		bool ShouldTraceFitFrames() const;
		void SetGizmoOperation(Core::EGizmoOperation p_operation);
		Core::EGizmoOperation GetGizmoOperation() const;
		/** Takes an event the window layer already translated, see InputEvent. */
		void ReceiveEvent(const InputEvent& p_event) override;
		/** Drops the hover highlight and the highlighted gizmo axis.
		 *
		 * The highlight comes from the picking target, and that is only re-read while the
		 * cursor moves: a cursor that leaves the viewport produces no more moves, so without
		 * this the shape under it stayed lit until the cursor came back. */
		void ClearHoverHighlight();
		bool MouseHit(Maths::FVector3& out);
		bool MouseClipHit(Maths::FVector3& out,const Maths::FVector4& clipPlane);
		Editor::Rendering::PickingRenderPass::PickingResult GetPickResult();
		/** Where the last pick landed on the geometry: the world position the picking
		 * target writes next to the id of every pixel it draws (see
		 * PickingRenderPass::PickHit), for the pixel the cursor is over.
		 * @return false when nothing with a position is under the cursor. */
		bool GetPickWorldPosition(Maths::FVector3& p_out) const;
		::Rendering::Geometry::Ray GetMouseRay();
		virtual ::Core::ECS::Actor* GetSelectedActor()override;
		virtual void SelectActor(::Core::ECS::Actor& actor)override;
		virtual void UnselectActor()override;
		virtual bool IsSelectActor()override;
	protected:
		virtual ::Core::Rendering::SceneRenderer::SceneDescriptor CreateSceneDescriptor() override;
	private:
		virtual void DrawFrame() override;
		/** Serves a pending RequestFitWhenReady once the scene has something to frame. */
		void TickRequestedFit();
		/** True when some model in the scene has bounds to frame. */
		bool HasModelToFrame();
		/** True when some model in the scene holds a triangle mesh with bounds, i.e. when a
		 * scene query build would have something to index. */
		bool HasTrianglesToQuery();
		void HandleActorPicking();
		/** Applies a pick result to the hover / selection state. */
		void ApplyPickResult(const Rendering::PickingRenderPass::PickingResult& p_result);
		/** Synchronous click pick against the picking target drawn this frame. */
		void ResolvePendingClick();
		/** Bounding sphere of the selected model; false when nothing selected has
		 * a model to fit. */
		bool GetSelectionSphere(::Rendering::Geometry::BoundingSphere& p_outSphere);
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
		::Core::Resources::MaterialAsset m_fallbackMaterial;
		Tools::Utils::OptRef<::Core::ECS::Actor> m_highlightedActor;
		std::optional<Editor::Core::GizmoBehaviour::EDirection> m_highlightedGizmoDirection;
		/** What the picking target last said about the cursor: the actor (or gizmo
		 * direction) that is drawn there, and the world position of that very pixel -
		 * which is the point the camera turns around when the right button is pressed. */
		Editor::Rendering::PickingRenderPass::PickHit pickingResult;
		/** Last mouse position the picking readback was performed for. */
		std::pair<double, double> m_lastPickingMouse{ -1.0, -1.0 };
		/** Throttles hover picks: the picking pass is a full scene pass, so it is
		 * rendered on demand at most every kPickThrottleMs milliseconds.
		 * A clock of our own, not a window toolkit's: the view is the render module's. */
		std::chrono::steady_clock::time_point m_pickRequestTime = std::chrono::steady_clock::now();
		/** View projection of the last pick request (refreshes the target while
		 * the camera moves). */
		Maths::FMatrix4 m_lastPickViewProjection = Maths::FMatrix4::Identity;
		bool m_hasLastPickViewProjection = false;
		/** Left click waiting for the synchronous pick on this frame's target. */
		bool m_clickPickPending = false;
		int m_clickPickX = 0;
		int m_clickPickY = 0;

		/** A read is framed as soon as the scene has a model to frame - see
		 * RequestFitWhenReady - and gives up after this many frames, so an unreadable file
		 * cannot leave a fit waiting to fire when a later feature adds one. */
		static constexpr int kFitTimeoutFrames = 600;
		bool m_fitPending = false;
		int m_fitFramesLeft = 0;
		/** Run in the frame the pending fit happened; may be empty. */
		std::function<void()> m_onFitted;

		/** The view's subscription to Actor::DestroyedEvent, which is static: it has to be
		 * removed when the view goes, or the event keeps calling into a view that is not
		 * there any more (see the destructor). */
		std::optional<Tools::Eventing::ListenerID> m_actorDestroyedListener;
	};
}
