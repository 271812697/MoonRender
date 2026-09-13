
#include <Core/ECS/Components/CAmbientBoxLight.h>
#include <Core/ECS/Components/CAmbientSphereLight.h>

#include "CameraController.h"
#include "GizmoRenderPass.h"
#include "Interactive/Screen/ScreenOverlayRegistry.h"
#include <iostream>
#include <algorithm>
#include <cmath>


Editor::Core::MirrorPlane ComputeMirrorPlane(const Maths::FVector3& CameraPos, const Maths::FVector3& CameraForward, const Maths::FVector3& BBoxMin, const Maths::FVector3& BBoxMax)
{
	Maths::FVector3 Center = (BBoxMin + BBoxMax) * 0.5f;
	Maths::FVector3 Extent = (BBoxMax - BBoxMin) * 0.5f;

	Editor::Core::MirrorPlane Faces[6];

	Faces[0] = { Maths::FVector3(-1,0,0), Maths::FVector3(BBoxMin.x, Center.y, Center.z), 0.0f };
	Faces[1] = { Maths::FVector3(1,0,0), Maths::FVector3(BBoxMax.x, Center.y, Center.z), 0.0f };
	Faces[2] = { Maths::FVector3(0,-1,0), Maths::FVector3(Center.x, BBoxMin.y, Center.z), 0.0f };
	Faces[3] = { Maths::FVector3(0,1,0), Maths::FVector3(Center.x, BBoxMax.y, Center.z), 0.0f };
	Faces[4] = { Maths::FVector3(0,0,-1), Maths::FVector3(Center.x, Center.y, BBoxMin.z), 0.0f };
	Faces[5] = { Maths::FVector3(0,0,1), Maths::FVector3(Center.x, Center.y, BBoxMax.z), 0.0f };

	float totalArea = (Extent.x * Extent.y + Extent.y * Extent.z + Extent.x * Extent.z) * 2.0f;

	for (int i = 0; i < 6; ++i)
	{
		float dotNormalForward = Maths::FVector3::Dot(Faces[i].Normal, CameraForward);
		float dist = Maths::FVector3::Dot(Faces[i].Normal, CameraPos - Faces[i].Point);

		bool isFront = dist < 0.0f;
		bool isHorizontal = (i == 2 || i == 3);

		float facingScore = 0.0f;
		if (isFront && dotNormalForward < 0.0f)
			facingScore = std::abs(dotNormalForward);

		float distScore = 0.0f;
		if (isFront)
		{
			float d = std::abs(dist);
			distScore = 1.0f / (1.0f + d * 0.15f);
		}

		float areaScore = 0.1f;
		if (i == 0 || i == 1) areaScore = Extent.y * Extent.z;
		if (i == 2 || i == 3) areaScore = Extent.x * Extent.z;
		if (i == 4 || i == 5) areaScore = Extent.x * Extent.y;
		areaScore = std::clamp(areaScore / (totalArea * 0.3f), 0.1f, 1.0f);

		float horizontalBonus = isHorizontal ? 1.8f : 1.0f;
		float finalScore = (facingScore * 0.5f + distScore * 0.3f + areaScore * 0.2f) * horizontalBonus;

		Faces[i].Score = isFront ? finalScore : 0.0f;
	}

	int bestIdx = 0;
	for (int i = 1; i < 6; ++i)
	{
		if (Faces[i].Score > Faces[bestIdx].Score)
			bestIdx = i;
	}

	return Faces[bestIdx];
}
Editor::Core::CameraController::CameraController(
	Editor::Panels::AView& p_view,
	::Rendering::Entities::Camera& p_camera
) :
	m_view(p_view),
	m_camera(p_camera)
{
	m_camera.SetFov(60.0f);
	mirrorPlane= { Maths::FVector3(0,1,0),Maths::FVector3(0,0,0),0 };
}

float GetActorFocusDist(Core::ECS::Actor& p_actor)
{
	float distance = 4.0f;

	if (p_actor.IsActive())
	{
		if (auto modelRenderer = p_actor.GetComponent<Core::ECS::Components::CModelRenderer>())
		{
			const bool hasCustomBoundingSphere = modelRenderer->GetFrustumBehaviour() == Core::ECS::Components::CModelRenderer::EFrustumBehaviour::CUSTOM_BOUNDS;
			const bool hasModel = modelRenderer->GetModel();
			const auto boundingSphere = hasCustomBoundingSphere ? &modelRenderer->GetCustomBoundingSphere() : hasModel ? &modelRenderer->GetModel()->GetBoundingSphere() : nullptr;
			const auto& actorPosition = p_actor.transform.GetWorldPosition();
			const auto& actorScale = p_actor.transform.GetWorldScale();
			const auto scaleFactor = std::max(std::max(actorScale.x, actorScale.y), actorScale.z);

			distance = std::max(distance, boundingSphere ? (boundingSphere->radius + Maths::FVector3::Length(boundingSphere->position)) * scaleFactor * 2.0f : 10.0f);
		}

		for (auto child : p_actor.GetChildren())
			distance = std::max(distance, GetActorFocusDist(*child));
	}

	return distance;
}

void ::Editor::Core::CameraController::HandleInputs(float p_deltaTime)
{
	HandleMouseReleased();
	HandleMousePressed();
	UpdateDepthRange();
	auto& input = m_view.getInutState();
	if (m_view.IsSelectActor()) {


		if (input.IsKeyPressed(Editor::Panels::KEYF))
		{
			auto& target = *m_view.GetSelectedActor();
			MoveToTarget(target);
		}

		auto focusObjectFromAngle = [this](const Maths::FVector3& offset)
			{
				auto& target = *m_view.GetSelectedActor();
				auto targetPos = target.transform.GetWorldPosition();
				float dist = GetActorFocusDist(target);
				auto camPos = targetPos + offset * dist;
				auto direction = Maths::FVector3::Normalize(targetPos - camPos);
				m_camera.SetRotation(Maths::FQuaternion::LookAt(direction, abs(direction.y) == 1.0f ? Maths::FVector3::Right : Maths::FVector3::Up));
				m_cameraDestinations.push({ camPos, m_camera.GetRotation() });
			};

		if (input.IsKeyPressed(Editor::Panels::UP))		focusObjectFromAngle(Maths::FVector3::Up);
		if (input.IsKeyPressed(Editor::Panels::DOWN))		focusObjectFromAngle(-Maths::FVector3::Up);
		if (input.IsKeyPressed(Editor::Panels::RIGHT))	focusObjectFromAngle(Maths::FVector3::Right);
		if (input.IsKeyPressed(Editor::Panels::LEFT))		focusObjectFromAngle(-Maths::FVector3::Right);
		if (input.IsKeyPressed(Editor::Panels::PageUp))	focusObjectFromAngle(Maths::FVector3::Forward);
		if (input.IsKeyPressed(Editor::Panels::PageDown))	focusObjectFromAngle(-Maths::FVector3::Forward);
	}

	if (!m_cameraDestinations.empty())
	{
		m_currentMovementSpeed = 0.0f;

		while (m_cameraDestinations.size() != 1)
			m_cameraDestinations.pop();

		auto& [destPos, destRotation] = m_cameraDestinations.front();

		float t = m_focusLerpCoefficient * p_deltaTime;

		// The pose is only reached when both halves of it are: a fit that keeps
		// the position and only turns the view (the roll buttons of the view cube
		// turn about the view axis) would otherwise arrive on its first frame and
		// the turn would pop instead of animating.
		const float positionGap = Maths::FVector3::Distance(m_camera.GetPosition(), destPos);
		// Quaternions cover every rotation twice, so the sign of the dot product
		// carries no meaning here: |dot| = cos(half the angle between them).
		const float rotationDot = std::abs(std::clamp(
			Maths::FQuaternion::DotProduct(m_camera.GetRotation(), destRotation),
			0.0f,
			1.0f));
		const float rotationGapDegrees =
			2.0f * std::acos(rotationDot) * 180.0f / 3.14159265359f;

		if (positionGap <= 0.03f && rotationGapDegrees <= 0.25f)
		{
			m_camera.SetPosition(destPos);
			m_camera.SetRotation(destRotation);
			m_cameraDestinations.pop();
		}
		else
		{
			m_camera.SetPosition(Maths::FVector3::Lerp(m_camera.GetPosition(), destPos, t));
			m_camera.SetRotation(Maths::FQuaternion::Lerp(m_camera.GetRotation(), destRotation, t));
		}
	}
	else
	{   
		// A screen widget that captured the cursor (a dragged slider or handle)
		// owns the mouse until it is released, so the camera stays out of it.
		if (MOON::ScreenOverlayRegistry::Instance().IsCapturing())
		{
			HandleCameraZoom();
			HandleCameraFPSKeyboard(p_deltaTime);
			return;
		}

		auto [xPos, yPos] = input.GetMousePosition();
		if (m_rightMousePressed || m_middleMousePressed || m_leftMousePressed)
		{

			bool wasFirstMouse = m_firstMouse;
			if (m_firstMouse)
			{
				m_lastMousePosX = xPos;
				m_lastMousePosY = yPos;
				
				HandleFirstMouse();
				m_firstMouse = false;
			}
			Maths::FVector2 mouseOffset
			{
				static_cast<float>(xPos - m_lastMousePosX),
				static_cast<float>(m_lastMousePosY - yPos)
			};

			m_lastMousePosX = xPos;
			m_lastMousePosY = yPos;
			if (m_rightMousePressed&&m_enableRotate)
			{	
				HandleCameraOrbit(m_view.GetRoaterCenter(), mouseOffset, wasFirstMouse);
			}
			else if (m_middleMousePressed)
			{
                HandleCameraPanning(mouseOffset, wasFirstMouse);
			}
			else
			{
				//HandleCameraFPSMouse(mouseOffset, wasFirstMouse);
			}
		}
		HandleCameraZoom();
		HandleCameraFPSKeyboard(p_deltaTime);
	}
}

void ::Editor::Core::CameraController::UpdateDepthRange()
{
	// Keep the near/far planes proportional to the camera distance so the
	// float32 depth buffer keeps enough precision on large models. With a tiny
	// fixed near plane (0.1) and a huge far plane, surfaces on the far side of
	// a large model lose precision and z-fight when the camera moves.
	Maths::FVector3 sceneCenter = m_view.GetRoaterCenter();
	float sceneRadius = 0.0f;
	if (auto* bvh = m_view.GetScene()->GetBvh()) {
		const auto& b = bvh->m_bounds;
		sceneCenter = (b.pmin + b.pmax) * 0.5f;
		sceneRadius = Maths::FVector3::Length(b.pmax - b.pmin) * 0.5f;
	}
	const float dist = Maths::FVector3::Length(m_camera.GetPosition() - sceneCenter);
	const float far = std::max(dist + sceneRadius * 2.0f, 1200.0f);
	const float near = std::max(dist * 0.01f, 0.001f);
	m_camera.SetNear(near);
	m_camera.SetFar(far);
}
void Editor::Core::CameraController::HandleFirstMouse()
{
	m_orbitStartOffset = m_camera.GetPosition() - m_view.GetRoaterCenter();
	m_orbitStartRotation = m_camera.GetRotation();
	m_orbitRotation = Maths::FQuaternion::Identity;
}

void Editor::Core::CameraController::MoveToTarget(::Core::ECS::Actor& p_target)
{
	m_cameraDestinations.push({
		p_target.transform.GetWorldPosition() -
		m_camera.GetRotation() *
		Maths::FVector3::Forward *
		GetActorFocusDist(p_target),
		m_camera.GetRotation()
		});
}

void Editor::Core::CameraController::MoveToPose(const Maths::FVector3& pos, const Maths::FQuaternion& quat)
{
	m_cameraDestinations.push({pos,quat});
}

void Editor::Core::CameraController::SetSpeed(float p_speed)
{
	m_cameraMoveSpeed = p_speed;
}

float Editor::Core::CameraController::GetSpeed() const
{
	return m_cameraMoveSpeed;
}

void Editor::Core::CameraController::SetPosition(const Maths::FVector3& p_position)
{
	m_camera.SetPosition(p_position);
}

void Editor::Core::CameraController::SetRotation(const Maths::FQuaternion& p_rotation)
{
	m_camera.SetRotation(p_rotation);
}

const Maths::FVector3& Editor::Core::CameraController::GetPosition() const
{
	return m_camera.GetPosition();
}

const Maths::FQuaternion& Editor::Core::CameraController::GetRotation() const
{
	return m_camera.GetRotation();
}

bool Editor::Core::CameraController::IsRightMousePressed() const
{
	return m_rightMousePressed;
}

bool Editor::Core::CameraController::IsOperating() const
{
	return m_rightMousePressed || m_middleMousePressed;
}

void Editor::Core::CameraController::LockTargetActor(::Core::ECS::Actor& p_actor)
{
	m_lockedActor = p_actor;
}

void Editor::Core::CameraController::UnlockTargetActor()
{
	m_lockedActor = std::nullopt;
}

void Editor::Core::CameraController::EnableRotate(bool flag)
{
	m_enableRotate = flag;
}

bool Editor::Core::CameraController::TryGetPendingPose(
	Maths::FVector3& p_outPosition,
	Maths::FQuaternion& p_outRotation) const
{
	if (m_cameraDestinations.empty())
	{
		return false;
	}
	// HandleInputs() keeps a single element in the queue, so the newest target
	// is the one the camera will end up on.
	const auto& destination = m_cameraDestinations.back();
	p_outPosition = std::get<0>(destination);
	p_outRotation = std::get<1>(destination);
	return true;
}

std::optional<std::reference_wrapper<Core::ECS::Actor>> Editor::Core::CameraController::GetTargetActor() const
{
	if (m_lockedActor.has_value())
	{
		return m_lockedActor;
	}

	return std::nullopt;
}

void Editor::Core::CameraController::HandleCameraPanning(const Maths::FVector2& p_mouseOffset, bool p_firstMouset)
{
	// Pass the scene size so the perspective pan scales with the model
	// (mirrors the zoom behavior which also uses the scene bounds).
	float sceneSize = 0.0f;
	if (auto* bvh = m_view.GetScene()->GetBvh()) {
		sceneSize = bvh->m_bounds.extents().Length();
	}
	m_camera.HandleCameraPanning(p_mouseOffset, m_cameraDragSpeed, sceneSize);
}

void Editor::Core::CameraController::HandleCameraOrbit(
	const Maths::FVector3& center,
	const Maths::FVector2& p_mouseOffset,
	bool p_firstMouse
)
{
	auto mouseOffset = p_mouseOffset * m_cameraOrbitSpeed;

	const float offsetX = -mouseOffset.x;	// yaw delta in degrees (around camera up)
	const float offsetY = -mouseOffset.y;	// pitch delta in degrees (around camera right)

	auto yAxis = m_camera.GetTransform().GetWorldUp();
	auto xAxis = m_camera.GetTransform().GetWorldRight();
	auto rAxis = yAxis * offsetX + xAxis * offsetY;

	constexpr float kPi = 3.14159265359f;
	float angleRad = rAxis.Length() / 180.0f * kPi;
	rAxis = rAxis.Normalize();

	auto quat = Maths::FQuaternion(rAxis, angleRad);

	// Accumulate the orbit rotation in world space, then rebuild the camera pose
	// from the captured start offset/rotation instead of chaining from the
	// previous frame's position. This avoids float32 error accumulation and the
	// jitter of the rotation center when the orbit radius / coordinates are large.
	m_orbitRotation = Maths::FQuaternion::Normalize(quat * m_orbitRotation);

	m_camera.SetPosition(center + m_orbitRotation * m_orbitStartOffset);
	m_camera.SetRotation(m_orbitRotation * m_orbitStartRotation);
}

void Editor::Core::CameraController::HandleCameraZoom()
{
	const auto verticalScroll = m_view.getInutState().GetMouseScroll().second;
	auto& input = m_view.getInutState();
	auto [x, y] = input.GetMousePosition();
	if (m_camera.GetProjectionMode() == ::Rendering::Settings::EProjectionMode::PERSPECTIVE)
	{
		auto bvh=m_view.GetScene()->GetBvh();;
		if (bvh) {
			bvh->m_bounds.extents().Length();
			m_camera.PersertiveZoom(verticalScroll * 0.1f * bvh->m_bounds.extents().Length());
		}
		else
		{
			m_camera.PersertiveZoom(verticalScroll);
		}
	}
	else
	{
		m_camera.OrthZoom(verticalScroll,x,y);
	}
}

void Editor::Core::CameraController::HandleCameraFPSMouse(const Maths::FVector2& p_mouseOffset, bool p_firstMouse)
{

}

void Editor::Core::CameraController::HandleCameraFPSKeyboard(float p_deltaTime)
{
	m_targetSpeed = Maths::FVector3(0.f, 0.f, 0.f);
	auto& input = m_view.getInutState();
	if (m_rightMousePressed)
	{
		float velocity = m_cameraMoveSpeed * p_deltaTime * 2.0f;

		if (input.IsKeyDown(Editor::Panels::KEYW))
			m_targetSpeed += m_camera.transform->GetWorldForward() * velocity;
		if (input.IsKeyDown(Editor::Panels::KEYS))
			m_targetSpeed += m_camera.transform->GetWorldForward() * -velocity;
		if (input.IsKeyDown(Editor::Panels::KEYA))
			m_targetSpeed += m_camera.transform->GetWorldRight() * velocity;
		if (input.IsKeyDown(Editor::Panels::KEYD))
			m_targetSpeed += m_camera.transform->GetWorldRight() * -velocity;
		if (input.IsKeyDown(Editor::Panels::KEYE))
			m_targetSpeed += {0.0f, velocity, 0.0f};
		if (input.IsKeyDown(Editor::Panels::KEYQ))
			m_targetSpeed += {0.0f, -velocity, 0.0f};

	}

	m_currentMovementSpeed = Maths::FVector3::Lerp(m_currentMovementSpeed, m_targetSpeed, 10.0f * p_deltaTime);
	m_camera.SetPosition(m_camera.GetPosition() + m_currentMovementSpeed);
}

void Editor::Core::CameraController::HandleMousePressed()
{
	auto& input = m_view.getInutState();
	if (input.IsMouseButtonPressed(Editor::Panels::MouseButton::MOUSE_BUTTON_LEFT))
	{
		m_leftMousePressed = true;
	}

	if (input.IsMouseButtonPressed(Editor::Panels::MouseButton::MOUSE_BUTTON_MIDDLE))
	{
		m_middleMousePressed = true;
	}

	if (input.IsMouseButtonPressed(Editor::Panels::MouseButton::MOUSE_BUTTON_RIGHT))
	{
		m_rightMousePressed = true;
	}
}

void Editor::Core::CameraController::HandleMouseReleased()
{
	auto& input = m_view.getInutState();
	if (m_leftMousePressed && input.IsMouseButtonReleased(Editor::Panels::MouseButton::MOUSE_BUTTON_LEFT))
	{
		m_leftMousePressed = false;
		m_firstMouse = true;
	}

	if (m_middleMousePressed && input.IsMouseButtonReleased(Editor::Panels::MouseButton::MOUSE_BUTTON_MIDDLE))
	{
		m_middleMousePressed = false;
		m_firstMouse = true;
	}

	if (m_rightMousePressed && input.IsMouseButtonReleased(Editor::Panels::MouseButton::MOUSE_BUTTON_RIGHT))
	{
		m_rightMousePressed = false;
		m_firstMouse = true;
		auto box = m_view.GetScene()->GetSceneBoundingBox();
		if (box.isValid() ) {
			box = box.addgap(0.1);
			Maths::FVector3 cameraForward = m_camera.GetTransform().GetWorldForward();
			Maths::FVector3 cameraPos = m_camera.GetPosition();
			mirrorPlane = ComputeMirrorPlane(cameraPos, cameraForward, box.pmin, box.pmax);
		}
	}
}
