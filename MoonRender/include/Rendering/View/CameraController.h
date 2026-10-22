#pragma once
#include <queue>
#include <functional>
#include <Rendering/Entities/Camera.h>

#include <Rendering/View/AView.h>

namespace Editor::Core
{
	struct MirrorPlane { Maths::FVector3 Normal; Maths::FVector3 Point; float Score; };
	class CameraController
	{
	public:
		/** Whether something (a screen widget being dragged, say) has captured the mouse, which
		 * keeps the camera out of it. The module cannot know that: the host installs the query. */
		using InputCaptureQuery = std::function<bool()>;

		CameraController(
			Editor::Panels::AView& p_view,
			::Rendering::Entities::Camera& p_camera
		);

		void SetInputCaptureQuery(InputCaptureQuery p_query);

		void HandleInputs(float p_deltaTime);
		void HandleFirstMouse();
		void MoveToTarget(::Core::ECS::Actor& p_target);
		void MoveToPose(const Maths::FVector3& pos, const Maths::FQuaternion& quat);
		/** Drops a pending move and any stale mouse-pressed state, so a new MoveToPose can run. */
		void ClearMovement();
		/** True while a MoveToPose/MoveToTarget animation is still playing. */
		bool HasPendingMovement() const;
		void SetSpeed(float p_speed);
		float GetSpeed() const;
		/** Multiplies the wheel-zoom step. The step is proportional to the scene's extent, which
		 * makes it far too small for a preview scene a couple of units across; a view can raise
		 * it without touching the main viewport. */
		void SetZoomSpeed(float p_speed);
		void SetPosition(const Maths::FVector3& p_position);
		void SetRotation(const Maths::FQuaternion& p_rotation);
		const Maths::FVector3& GetPosition() const;
		const Maths::FQuaternion& GetRotation() const;
		bool IsRightMousePressed() const;
		bool IsOperating() const;
		void LockTargetActor(::Core::ECS::Actor& p_actor);
		void UnlockTargetActor();
		void EnableRotate(bool flag);
		/** False while the view is locked, e.g. while sketching. */
		bool IsRotateEnabled() const { return m_enableRotate; }
		/** Pose the camera is currently animating towards.
		 *
		 * Returns false when no MoveToPose() target is queued, in which case the
		 * outputs are left untouched. */
		bool TryGetPendingPose(
			Maths::FVector3& p_outPosition,
			Maths::FQuaternion& p_outRotation) const;
		MirrorPlane GetMirrorPlane() {return  mirrorPlane;
		}
	private:
		std::optional<std::reference_wrapper<::Core::ECS::Actor>> GetTargetActor() const;
		void HandleCameraPanning(const Maths::FVector2& p_mouseOffset, bool p_firstMouse);
		void HandleCameraOrbit(const Maths::FVector3& center, const Maths::FVector2& p_mouseOffset, bool p_firstMouse);
		void HandleCameraFPSMouse(const Maths::FVector2& p_mouseOffset, bool p_firstMouse);
		void UpdateDepthRange();

		void HandleCameraZoom();
		void HandleCameraFPSKeyboard(float p_deltaTime);
		void HandleMousePressed();
		void HandleMouseReleased();

	private:
		Editor::Panels::AView& m_view;
		::Rendering::Entities::Camera& m_camera;
		/** Installed by the host, see InputCaptureQuery. Empty means nothing captures the mouse. */
		InputCaptureQuery m_isInputCaptured;
		std::queue<std::tuple<Maths::FVector3, Maths::FQuaternion>> m_cameraDestinations;
		bool m_leftMousePressed = false;
		bool m_middleMousePressed = false;
		bool m_rightMousePressed = false;
		bool m_enableRotate = true;

		Maths::FVector3 m_targetSpeed;
		Maths::FVector3 m_currentMovementSpeed;

		Maths::FTransform* m_orbitTarget = nullptr;
		Maths::FVector3 m_orbitStartOffset;
		Maths::FQuaternion m_orbitStartRotation;
		Maths::FQuaternion m_orbitRotation;
		bool m_firstMouse = true;
		double m_lastMousePosX = 0.0;
		double m_lastMousePosY = 0.0;
		float m_mouseSensitivity = 0.12f;
		float m_cameraDragSpeed = 1.0f;
		float m_cameraOrbitSpeed = 0.5f;
		float m_cameraMoveSpeed = 15.0f;
		float m_zoomSpeed = 1.0f;
		float m_focusDistance = 15.0f;
		float m_focusLerpCoefficient = 8.0f;

		std::optional<std::reference_wrapper<::Core::ECS::Actor>> m_lockedActor = std::nullopt;
		MirrorPlane mirrorPlane;
	};
}
