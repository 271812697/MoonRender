#include "Interactive/Widgets/ViewCubeWidget.h"
#include "Interactive/Im3DType.h"
#include "Interactive/Im3DRenderer.h"
#include "renderer/SceneView.h"
#include "renderer/CameraController.h"
#include <algorithm>

namespace MOON
{
	namespace
	{
		/** Distance between the cube center and a button center. The cube
		 * silhouette stays inside a 51px radius in the worst case, so the ring
		 * sits in the empty band around it. */
		constexpr float kButtonRadius = 52.0f;
		/** Half extent of the arrow head triangle. */
		constexpr float kButtonHalfSize = 9.0f;
		/** Extra pick area around each arrow, in pixels. */
		constexpr float kButtonHitPadding = 4.0f;

		constexpr float kDegToRad = 3.14159265358979f / 180.0f;

		/** Arrow head pointing along a unit direction: tip, base left, base
		 * right, centered on p_centerX / p_centerY. */
		std::vector<ImVec2> BuildArrowTriangle(
			float p_centerX,
			float p_centerY,
			float p_dirX,
			float p_dirY,
			float p_halfSize)
		{
			const float baseHalf = p_halfSize * 0.9f;
			return {
				ImVec2(p_centerX + p_dirX * p_halfSize, p_centerY + p_dirY * p_halfSize),
				ImVec2(
					p_centerX - p_dirX * p_halfSize - p_dirY * baseHalf,
					p_centerY - p_dirY * p_halfSize + p_dirX * baseHalf),
				ImVec2(
					p_centerX - p_dirX * p_halfSize + p_dirY * baseHalf,
					p_centerY - p_dirY * p_halfSize - p_dirX * baseHalf)
			};
		}
	}

	ViewCubeWidget::ViewCubeWidget(const std::string& name) : ScreenWidget(name)
	{
		// Always on: the buttons belong to the navigation cube, which is drawn
		// every frame. setActive() also subscribes the widget to the mouse and
		// key events of the interactor.
		setActive(true);
		SetHoverCursor(ImGuiMouseCursor_Hand);
	}

	ViewCubeWidget::~ViewCubeWidget()
	{
	}

	void ViewCubeWidget::SetStepDegrees(float p_degrees)
	{
		mStepDegrees = std::clamp(p_degrees, 1.0f, 180.0f);
	}

	ScreenLayout ViewCubeWidget::BuildLayout() const
	{
		// Same rectangle the navigation cube renders into, so the buttons land on
		// the cube and no extra viewport area is taken from the scene.
		ScreenLayout layout;
		layout.anchor = EScreenAnchor::TopRight;
		layout.offset = ImVec2(static_cast<float>(kViewCubeMargin), static_cast<float>(kViewCubeMargin));
		layout.size = ImVec2(static_cast<float>(kViewCubeSize), static_cast<float>(kViewCubeSize));
		return layout;
	}

	void ViewCubeWidget::BuildShapes(std::vector<HitShape>& p_outShapes) const
	{
		const float center = static_cast<float>(kViewCubeSize) * 0.5f;

		// Screen space directions, y grows downwards, one button per side.
		struct ButtonDefinition
		{
			EAction action;
			float dirX;
			float dirY;
		};
		static const ButtonDefinition definitions[] = {
			{ EAction::OrbitUp,    0.0f, -1.0f },
			{ EAction::OrbitRight, 1.0f,  0.0f },
			{ EAction::OrbitDown,  0.0f,  1.0f },
			{ EAction::OrbitLeft, -1.0f,  0.0f }
		};

		for (const ButtonDefinition& definition : definitions)
		{
			HitShape shape;
			shape.type = HitShape::EType::Triangle;
			shape.action = static_cast<int>(definition.action);
			shape.pad = kButtonHitPadding;
			shape.points = BuildArrowTriangle(
				center + definition.dirX * kButtonRadius,
				center + definition.dirY * kButtonRadius,
				definition.dirX,
				definition.dirY,
				kButtonHalfSize);
			p_outShapes.push_back(shape);
		}
	}

	void ViewCubeWidget::DrawContent(ImDrawList& p_drawList, const ScreenRect& p_rect)
	{
		DrawShapes(p_drawList, p_rect);
	}

	bool ViewCubeWidget::IsInteractionEnabled() const
	{
		if (m_sceneView == nullptr)
		{
			return false;
		}
		// Sketching locks the camera onto the sketch plane through the same flag
		// the orbit interactions use, so the buttons disappear with it.
		return m_sceneView->GetCameraController().IsRotateEnabled();
	}

	void ViewCubeWidget::OnAction(int p_action)
	{
		ApplyRotation(static_cast<EAction>(p_action));
	}

	void ViewCubeWidget::ApplyRotation(EAction p_action)
	{
		if (m_sceneView == nullptr)
		{
			return;
		}
		auto* camera = m_sceneView->GetCamera();
		if (camera == nullptr)
		{
			return;
		}

		Editor::Core::CameraController& controller = m_sceneView->GetCameraController();

		// Keep the distance, the orbit center and the roll untouched: the click
		// only re-orients the camera around the orbit center. When an animation is
		// still running the pose it is heading to is used, so several clicks in a
		// row add up instead of compounding from a half way pose.
		Maths::FVector3 pivotPosition = camera->GetPosition();
		Maths::FQuaternion pivotRotation = camera->GetRotation();
		controller.TryGetPendingPose(pivotPosition, pivotRotation);

		// Same axes and signs as CameraController::HandleCameraOrbit(), so a
		// click matches a drag of the same length in that direction.
		const Maths::FVector3 worldUp = camera->GetTransform().GetWorldUp();
		const Maths::FVector3 worldRight = camera->GetTransform().GetWorldRight();

		Maths::FVector3 axis = worldUp;
		float angle = mStepDegrees * kDegToRad;
		switch (p_action)
		{
		case EAction::OrbitLeft:
			axis = worldUp;
			break;
		case EAction::OrbitRight:
			axis = worldUp;
			angle = -angle;
			break;
		case EAction::OrbitUp:
			axis = worldRight;
			angle = -angle;
			break;
		case EAction::OrbitDown:
			axis = worldRight;
			break;
		default:
			return;
		}

		const Maths::FQuaternion delta(axis, angle);
		const Maths::FVector3 center = m_sceneView->GetRoaterCenter();
		const Maths::FVector3 offset = pivotPosition - center;
		controller.MoveToPose(center + delta * offset, delta * pivotRotation);
	}
}
