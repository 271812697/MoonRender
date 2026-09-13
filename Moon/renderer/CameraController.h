#pragma once
#include <queue>
#include <Rendering/Entities/Camera.h>

#include "AView.h"

namespace Editor::Core
{
	struct MirrorPlane { Maths::FVector3 Normal; Maths::FVector3 Point; float Score; };
	class CameraController
	{
	public:
		CameraController(
			Editor::Panels::AView& p_view,
			::Rendering::Entities::Camera& p_camera
		);

		void HandleInputs(float p_deltaTime);
		void HandleFirstMouse();
		void MoveToTarget(::Core::ECS::Actor& p_target);
		void MoveToPose(const Maths::FVector3& pos, const Maths::FQuaternion& quat);
		void SetSpeed(float p_speed);
		float GetSpeed() const;
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
		float m_focusDistance = 15.0f;
		float m_focusLerpCoefficient = 8.0f;

		std::optional<std::reference_wrapper<::Core::ECS::Actor>> m_lockedActor = std::nullopt;
		MirrorPlane mirrorPlane;
	};
}
