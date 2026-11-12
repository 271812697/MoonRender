#include "Interactive/Widgets/ViewCubeWidget.h"
#include "editor/UI/IconCache.h"
#include "Interactive/Im3DType.h"
#include "renderer/SceneView.h"
#include <Rendering/View/CameraController.h>
#include "core/log.h"
#include <algorithm>
#include <cmath>

namespace MOON
{
	namespace
	{
		/** Ring the buttons sit on: distance from the cube center to the middle of a
		 * button. The cube silhouette stays inside a 51px radius in the worst case,
		 * so the ring is clear of it. */
		constexpr float kButtonRingRadius = 58.0f;
		constexpr float kHalfPi = 1.5707963267948966f;
		/** The click target around an icon. The icons are drawn 13px half size (the
		 * two roll ones 16px, since their glyph reads smaller), so the circle takes
		 * the wider of the two and a few pixels of slack. */
		constexpr float kButtonHitRadius = 17.0f;
		constexpr float kButtonHitPadding = 4.0f;

		constexpr float kDegToRad = 3.14159265358979f / 180.0f;

		/** Axis a button turns the view around, in the camera's own frame. */
		enum class EStepAxis
		{
			/** About the camera up: a yaw, the view turns left / right. */
			Yaw,
			/** About the camera right: a pitch, the view looks up / down. */
			Pitch,
			/** About the camera forward: a roll, the view spins in place. */
			Roll
		};

		/** One button of the ring. */
		struct ButtonDefinition
		{
			ViewCubeWidget::EAction action;
			/** Where the button sits on the ring, in degrees, screen space with y
			 * down: -90 up, 0 right, 90 down, 180 left, 45 south east, 135 south
			 * west. */
			float placementDeg;
			EStepAxis axis;
			/** Sign of the step, so a click turns the view the way the button
			 * points. */
			float stepSign;
		};

		const ButtonDefinition kButtonDefinitions[] = {
			{ ViewCubeWidget::EAction::OrbitUp,              -90.0f, EStepAxis::Pitch, -1.0f },
			{ ViewCubeWidget::EAction::OrbitRight,             0.0f, EStepAxis::Yaw,   -1.0f },
			{ ViewCubeWidget::EAction::OrbitDown,             90.0f, EStepAxis::Pitch,  1.0f },
			{ ViewCubeWidget::EAction::OrbitLeft,            180.0f, EStepAxis::Yaw,    1.0f },
			/** The two roll buttons: they spin the view about the axis it looks
			 * along, at the south east and the south west of the ring. */
			{ ViewCubeWidget::EAction::RollClockwise,         45.0f, EStepAxis::Roll,  -1.0f },
			{ ViewCubeWidget::EAction::RollCounterClockwise, 135.0f, EStepAxis::Roll,   1.0f }
		};

	}

	ViewCubeWidget::ViewCubeWidget(const std::string& name) : ScreenWidget(name)
	{
		// Always on: the buttons belong to the navigation cube, which is drawn
		// every frame. setActive() also subscribes the widget to the mouse and
		// key events of the interactor.
		setActive(true);
		SetHoverCursor(EScreenCursor::Hand);
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
		layout.offset = ScreenPoint(static_cast<float>(kViewCubeMargin), static_cast<float>(kViewCubeMargin));
		layout.size = ScreenPoint(static_cast<float>(kViewCubeSize), static_cast<float>(kViewCubeSize));
		return layout;
	}

	void ViewCubeWidget::BuildShapes(std::vector<HitShape>& p_outShapes) const
	{
		// The buttons are their icons: the click target is a circle under each of them,
		// on the ring the icons are drawn on. Nothing is filled, so the circle is only a
		// hit area - the cursor turning into a hand is the whole of the feedback, and the
		// glyph is tinted on hover.
		const float center = static_cast<float>(kViewCubeSize) * 0.5f;

		for (const ButtonDefinition& definition : kButtonDefinitions)
		{
			const float axis = definition.placementDeg * kDegToRad;
			HitShape shape;
			shape.type = HitShape::EType::Circle;
			shape.action = static_cast<int>(definition.action);
			shape.center = ScreenPoint(
				center + std::cos(axis) * kButtonRingRadius,
				center + std::sin(axis) * kButtonRingRadius);
			shape.radius = kButtonHitRadius;
			shape.pad = kButtonHitPadding;
			p_outShapes.push_back(std::move(shape));
		}
	}

	void ViewCubeWidget::DrawContent(::Rendering::Overlay::Im2D::DrawList& p_drawList, const ScreenRect& p_rect)
	{
		// Only the icons are drawn; their click targets are invisible circles (see BuildShapes).
		DrawIcons(p_drawList, p_rect);
	}

	void ViewCubeWidget::DrawIcons(::Rendering::Overlay::Im2D::DrawList& p_drawList, const ScreenRect& p_rect)
	{
		// One glyph per button, centred on the ring the hit test already uses
		// (kButtonRingRadius). The icons are drawn white and tinted here, which is what lets a
		// button light up on hover without a second asset.
		struct IconFor { ViewCubeWidget::EAction action; const char* path; };
		static const IconFor kIcons[] = {
			{ ViewCubeWidget::EAction::OrbitUp,              ":/widgets/icons/viewcube32/ViewCube_RotateUp.svg" },
			{ ViewCubeWidget::EAction::OrbitRight,           ":/widgets/icons/viewcube32/ViewCube_RotateRight.svg" },
			{ ViewCubeWidget::EAction::OrbitDown,            ":/widgets/icons/viewcube32/ViewCube_RotateDown.svg" },
			{ ViewCubeWidget::EAction::OrbitLeft,            ":/widgets/icons/viewcube32/ViewCube_RotateLeft.svg" },
			{ ViewCubeWidget::EAction::RollClockwise,        ":/widgets/icons/viewcube32/ViewCube_RollRight.svg" },
			{ ViewCubeWidget::EAction::RollCounterClockwise, ":/widgets/icons/viewcube32/ViewCube_RollLeft.svg" },
		};

		const float cubeCenter = static_cast<float>(kViewCubeSize) * 0.5f;
		const int hot = HotShapeIndex();

		for (int index = 0; index < static_cast<int>(std::size(kButtonDefinitions)); ++index)
		{
			const ButtonDefinition& definition = kButtonDefinitions[index];
			const IconFor* icon = nullptr;
			for (const IconFor& candidate : kIcons)
			{
				if (candidate.action == definition.action)
				{
					icon = &candidate;
					break;
				}
			}
			if (icon == nullptr)
			{
				continue;
			}
			const auto texture = MOON::IconCache::Instance().Get(QString::fromLatin1(icon->path), 20);
			if (texture == 0)
			{
				continue;
			}

			const float axis = definition.placementDeg * kDegToRad;
			const float cx = p_rect.x + cubeCenter + std::cos(axis) * kButtonRingRadius;
			const float cy = p_rect.y + cubeCenter + std::sin(axis) * kButtonRingRadius;
			const ::Rendering::Overlay::Im2D::ImU32 tint = (index == hot)
				? ::Rendering::Overlay::Im2D::ColorU32(255, 214, 102, 255)
				: ::Rendering::Overlay::Im2D::ColorU32(255, 255, 255, 240);
			const float centreX = cx;
			const float centreY = cy;
			// The roll glyphs are an arc plus a head, which reads smaller than a solid arrow at the
			// same box size; they get a wider box so the head is still legible.
			const bool isRoll = definition.action == EAction::RollClockwise
				|| definition.action == EAction::RollCounterClockwise;
			const float iconHalf = isRoll ? 16.0f : 13.0f;
			// The glyph itself points along +X, so each button gets the quarter turn that makes
			// it point the way it turns the view: the two vertical buttons carry vertical
			// arrows, the two horizontal ones horizontal arrows, the roll buttons stay upright.
			float glyphDegrees = 0.0f;
			switch (definition.action)
			{
			case EAction::OrbitUp:    glyphDegrees = 90.0f;  break;
			case EAction::OrbitDown:  glyphDegrees = -90.0f; break;
			case EAction::OrbitLeft:  glyphDegrees = 0.0f;   break;
			case EAction::OrbitRight: glyphDegrees = 180.0f; break;
			default:                  glyphDegrees = 0.0f;   break;
			}
			const float glyphRad = glyphDegrees * kDegToRad;
			const float rightX = std::cos(glyphRad);
			const float rightY = std::sin(glyphRad);
			const float upX = -rightY;
			const float upY = rightX;
			p_drawList.AddImageQuad(texture,
				::Rendering::Overlay::Im2D::Vec2(centreX - rightX * iconHalf - upX * iconHalf, centreY - rightY * iconHalf - upY * iconHalf),
				::Rendering::Overlay::Im2D::Vec2(centreX + rightX * iconHalf - upX * iconHalf, centreY + rightY * iconHalf - upY * iconHalf),
				::Rendering::Overlay::Im2D::Vec2(centreX + rightX * iconHalf + upX * iconHalf, centreY + rightY * iconHalf + upY * iconHalf),
				::Rendering::Overlay::Im2D::Vec2(centreX - rightX * iconHalf + upX * iconHalf, centreY - rightY * iconHalf + upY * iconHalf),
				tint);
		}
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

		const ButtonDefinition* definition = nullptr;
		for (const ButtonDefinition& candidate : kButtonDefinitions)
		{
			if (candidate.action == p_action)
			{
				definition = &candidate;
				break;
			}
		}
		if (definition == nullptr)
		{
			return;
		}

		// Work from the pose the camera is heading for: while a previous click is
		// still animating, a second one has to add to it instead of turning from a
		// half way pose.
		Maths::FVector3 posePosition = camera->GetPosition();
		Maths::FQuaternion poseRotation = camera->GetRotation();
		m_sceneView->GetCameraController().TryGetPendingPose(posePosition, poseRotation);

		// The step turns the camera about one of its own axes, the same axes and
		// signs CameraController::HandleCameraOrbit() uses for a drag: a yaw about
		// the camera up, a pitch about its right, and the two south buttons roll
		// the view about the direction it looks along.
		Maths::FVector3 axis = poseRotation * Maths::FVector3::Up;
		switch (definition->axis)
		{
		case EStepAxis::Yaw:
			axis = poseRotation * Maths::FVector3::Up;
			break;
		case EStepAxis::Pitch:
			axis = poseRotation * Maths::FVector3::Right;
			break;
		case EStepAxis::Roll:
			axis = poseRotation * Maths::FVector3::Forward;
			break;
		}

		const float angle = definition->stepSign * mStepDegrees * kDegToRad;
		const Maths::FQuaternion delta(axis, angle);
		// Compose the step with the pose the camera is on (or is heading for), so
		// the click turns that pose inside its own frame - the same thing as
		// turning the model around the camera's axes while the camera watches. A
		// pose that is not level keeps its tilt, which deriving the orientation
		// from a direction and the world up would have flattened.
		//
		// The fit then puts the camera back on the center of the focus sphere (the
		// selection when there is one, the scene otherwise) at the fitted distance,
		// so the model turns about that center and stays framed.
		m_sceneView->FitToFocusWithRotation(delta * poseRotation);
	}
}
