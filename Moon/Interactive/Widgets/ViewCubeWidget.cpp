#include "Interactive/Widgets/ViewCubeWidget.h"
#include "Interactive/Im3DType.h"
#include "Interactive/Im3DRenderer.h"
#include "Interactive/Screen/ScreenPath.h"
#include "Interactive/Screen/ShapeBuilder.h"
#include "renderer/SceneView.h"
#include "renderer/CameraController.h"
#include "core/log.h"
#include <algorithm>
#include <cmath>
#include <iterator>

namespace MOON
{
	namespace
	{
		/** Ring the buttons sit on: distance from the cube center to the center
		 * line of a slot, and half the width of a slot. The cube silhouette stays
		 * inside a 51px radius in the worst case, so the ring is clear of it. */
		constexpr float kSlotRadius = 70.0f;
		constexpr float kSlotHalfWidth = 7.0f;
		/** How far a slot reaches on either side of the axis it belongs to. The
		 * four side buttons sit 90 degrees apart, so they can afford a wide slot;
		 * the two roll buttons are squeezed between them at 45 degrees and are cut
		 * shorter, which keeps the gaps in the ring even. */
		constexpr float kOrbitSlotHalfSweepDeg = 16.0f;
		constexpr float kRollSlotHalfSweepDeg = 12.0f;
		/** Flattening tolerance of the curves, in pixels: a slot is about 80px
		 * long, so this is already smooth and keeps the loops short. */
		constexpr double kSlotDeflection = 0.05;
		/** Extra pick area around each slot, in pixels. */
		constexpr float kSlotHitPadding = 4.0f;
		/** Stroke width and pick slack of the outline, see HitShape::Path. */
		constexpr float kSlotStrokeWidth = 2.0f;
		constexpr float kSlotStrokePickSlack = 5.0f;

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
			/** Where the slot sits on the ring, in degrees, screen space with y
			 * down: -90 up, 0 right, 90 down, 180 left, 45 south east, 135 south
			 * west. */
			float placementDeg;
			EStepAxis axis;
			/** Sign of the step, so a click turns the view the way the button
			 * points. */
			float stepSign;
			/** Half of the angle the slot spans. */
			float halfSweepDeg;
		};

		const ButtonDefinition kButtonDefinitions[] = {
			{ ViewCubeWidget::EAction::OrbitUp,              -90.0f, EStepAxis::Pitch, -1.0f, kOrbitSlotHalfSweepDeg },
			{ ViewCubeWidget::EAction::OrbitRight,             0.0f, EStepAxis::Yaw,   -1.0f, kOrbitSlotHalfSweepDeg },
			{ ViewCubeWidget::EAction::OrbitDown,             90.0f, EStepAxis::Pitch,  1.0f, kOrbitSlotHalfSweepDeg },
			{ ViewCubeWidget::EAction::OrbitLeft,            180.0f, EStepAxis::Yaw,    1.0f, kOrbitSlotHalfSweepDeg },
			/** The two roll buttons: they spin the view about the axis it looks
			 * along, at the south east and the south west of the ring. */
			{ ViewCubeWidget::EAction::RollClockwise,         45.0f, EStepAxis::Roll,  -1.0f, kRollSlotHalfSweepDeg },
			{ ViewCubeWidget::EAction::RollCounterClockwise, 135.0f, EStepAxis::Roll,   1.0f, kRollSlotHalfSweepDeg }
		};

		/** Arrow head pointing along a unit direction: tip, base left, base
		 * right, centered on p_centerX / p_centerY. Only used when the slot
		 * curves cannot be built. */
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
		BuildArrowShapes();
	}

	ViewCubeWidget::~ViewCubeWidget()
	{
	}

	void ViewCubeWidget::SetStepDegrees(float p_degrees)
	{
		mStepDegrees = std::clamp(p_degrees, 1.0f, 180.0f);
	}

	void ViewCubeWidget::BuildArrowShapes()
	{
		const float center = static_cast<float>(kViewCubeSize) * 0.5f;

		mArrows.clear();
		mArrows.reserve(std::size(kButtonDefinitions));
		for (const ButtonDefinition& definition : kButtonDefinitions)
		{
			// One arc slot per button, centered on the axis of that button, so the
			// six of them read as one ring of curved buttons around the cube.
			ShapeBuilder builder;
			builder.ArcSlot(
				center,
				center,
				kSlotRadius,
				kSlotHalfWidth,
				definition.placementDeg - definition.halfSweepDeg,
				2.0f * definition.halfSweepDeg);

			std::vector<ScreenPath> paths = builder.BuildPaths({ kSlotDeflection });
			if (paths.empty())
			{
				CORE_WARN(
					"[ViewCube] the arc slot of the {0} button does not enclose a face",
					static_cast<int>(definition.action));
				continue;
			}
			if (paths.size() > 1)
			{
				CORE_WARN(
					"[ViewCube] the {0} button describes {1} faces instead of 1; the "
					"first one is used",
					static_cast<int>(definition.action),
					paths.size());
			}

			ArrowShape arrow;
			arrow.action = definition.action;
			arrow.path = std::make_shared<const ScreenPath>(std::move(paths.front()));
			mArrows.push_back(std::move(arrow));
		}

		if (mArrows.size() != std::size(kButtonDefinitions))
		{
			CORE_WARN(
				"[ViewCube] only {0} of {1} button slots could be built; the plain "
				"arrow heads are used instead",
				mArrows.size(),
				std::size(kButtonDefinitions));
			mArrows.clear();
			return;
		}

		size_t points = 0;
		for (const ArrowShape& arrow : mArrows)
		{
			for (const std::vector<ImVec2>& loop : arrow.path->loops)
			{
				points += loop.size();
			}
			// Ask for the fill once: an outline that cannot be triangulated would
			// otherwise only show up as a button drawn without its fill, and the
			// reason is logged by ScreenPath.
			if (arrow.path->GetFillTriangles().empty())
			{
				CORE_WARN(
					"[ViewCube] a button outline cannot be filled; it is drawn as an "
					"outline only");
			}
		}
		CORE_INFO(
			"[ViewCube] built {0} button arc slots from OCCT curves ({1} points in "
			"total)",
			mArrows.size(),
			points);
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
		if (mArrows.empty())
		{
			BuildFallbackShapes(p_outShapes);
			return;
		}

		for (const ArrowShape& arrow : mArrows)
		{
			if (!arrow.path || arrow.path->IsEmpty())
			{
				continue;
			}
			HitShape shape;
			shape.type = HitShape::EType::Path;
			shape.action = static_cast<int>(arrow.action);
			shape.path = arrow.path;
			// The outline is part of the shape as well: a slot is only 14px wide,
			// so the fill alone would leave a small click target.
			shape.pickMode = HitShape::EPickMode::FillOrStroke;
			shape.pad = kSlotHitPadding;
			shape.strokeWidth = kSlotStrokeWidth;
			shape.strokePickSlack = kSlotStrokePickSlack;
			p_outShapes.push_back(std::move(shape));
		}
	}

	void ViewCubeWidget::BuildFallbackShapes(std::vector<HitShape>& p_outShapes) const
	{
		const float center = static_cast<float>(kViewCubeSize) * 0.5f;
		constexpr float kFallbackRadius = 52.0f;
		constexpr float kFallbackHalfSize = 9.0f;

		for (const ButtonDefinition& definition : kButtonDefinitions)
		{
			const float axis = definition.placementDeg * kDegToRad;
			const float dirX = std::cos(axis);
			const float dirY = std::sin(axis);

			HitShape shape;
			shape.type = HitShape::EType::Triangle;
			shape.action = static_cast<int>(definition.action);
			shape.pad = kSlotHitPadding;
			shape.points = BuildArrowTriangle(
				center + dirX * kFallbackRadius,
				center + dirY * kFallbackRadius,
				dirX,
				dirY,
				kFallbackHalfSize);
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
