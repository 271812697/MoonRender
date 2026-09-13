#include "Interactive/Widgets/PathShapeWidget.h"
#include "Interactive/Screen/ScreenPath.h"
#include "Interactive/Screen/SketchPathBake.h"
#include "Sketcher/SketcherObj.h"
#include "Sketcher/SketcherObjManager.h"
#include "core/log.h"
#include <cmath>

namespace MOON
{
	namespace
	{
		constexpr float kWidgetOffsetX = 48.0f;
		constexpr float kWidgetOffsetY = 48.0f;
		constexpr float kWidgetSize = 220.0f;

	}

	PathShapeWidget::PathShapeWidget(const std::string& name) : ScreenWidget(name)
	{
		// Activation is left to the owner: the pass settings panel toggles this
		// widget so it can be used as a reference while wiring a real one up.
		// Only the shape itself owns the cursor: the empty corners of the widget
		// rectangle still belong to the scene.
		SetRectBlocksCursor(false);
		SetHoverCursor(ImGuiMouseCursor_Hand);
	}

	PathShapeWidget::~PathShapeWidget()
	{
	}

	ScreenLayout PathShapeWidget::BuildLayout() const
	{
		ScreenLayout layout;
		layout.anchor = EScreenAnchor::TopLeft;
		layout.offset = ImVec2(kWidgetOffsetX, kWidgetOffsetY);
		layout.size = ImVec2(kWidgetSize, kWidgetSize);
		return layout;
	}

	void PathShapeWidget::BuildFallbackWires(std::vector<ScreenPath>& p_wires) const
	{
		// A five pointed star: concave on purpose, so it exercises the concave
		// fill and the even-odd hit test.
		constexpr int kSpikes = 5;
		constexpr float kOuterRadius = 100.0f;
		constexpr float kInnerRadius = 42.0f;
		constexpr float kPi = 3.14159265358979f;

		std::vector<ImVec2> loop;
		loop.reserve(kSpikes * 2);
		for (int i = 0; i < kSpikes * 2; ++i)
		{
			// Alternating outer / inner vertices. This is y up space, so the
			// first vertex at -90 degrees ends up pointing up once FitInto()
			// flips the shape into screen space.
			const float angle = -kPi * 0.5f + static_cast<float>(i) * kPi / kSpikes;
			const float radius = (i % 2 == 0) ? kOuterRadius : kInnerRadius;
			loop.emplace_back(radius * std::cos(angle), radius * std::sin(angle));
		}

		ScreenPath star;
		star.loops.push_back(std::move(loop));
		star.closed.push_back(true);
		star.RecomputeBounds();
		p_wires.push_back(std::move(star));

		// A second wire next to it: two wires must stay two separate shapes.
		ScreenPath circle;
		std::vector<ImVec2> circleLoop;
		constexpr int kCircleSegments = 32;
		constexpr float kCircleRadius = 34.0f;
		constexpr float kCircleCenterX = 150.0f;
		for (int i = 0; i < kCircleSegments; ++i)
		{
			const float angle = static_cast<float>(i) * 2.0f * kPi / kCircleSegments;
			circleLoop.emplace_back(
				kCircleCenterX + kCircleRadius * std::cos(angle),
				kCircleRadius * std::sin(angle));
		}
		circle.loops.push_back(std::move(circleLoop));
		circle.closed.push_back(true);
		circle.RecomputeBounds();
		p_wires.push_back(std::move(circle));
	}

	void PathShapeWidget::RefreshShape() const
	{
		SketcherObj* sketch = SketcherObjManager::instance().GetCurrentActiveSketcherObj();
		const int curveCount = (sketch != nullptr) ? sketch->getHighestCurveIndex() : -1;
		if (!mWirePaths.empty()
			&& sketch == mSourceSketch
			&& curveCount == mSourceCurveCount
			&& mSourceIsSketch == (sketch != nullptr))
		{
			return;
		}

		mSourceSketch = sketch;
		mSourceCurveCount = curveCount;
		mSourceIsSketch = sketch != nullptr;

		std::vector<ScreenPath> wires;
		if (sketch != nullptr)
		{
			wires = BakeSketchFaces(*sketch);
		}
		if (wires.empty())
		{
			CORE_INFO(
				"[PathShape] nothing baked from the active sketch, using the built-in shapes");
			BuildFallbackWires(wires);
		}

		// Sketch coordinates (y up) into the widget local rectangle (y down), with
		// one transform for the whole bake so the wires keep their layout.
		const ScreenRect localRect(0.0f, 0.0f, kWidgetSize, kWidgetSize);
		FitWiresInto(wires, localRect, true);

		mWirePaths.clear();
		mWirePaths.reserve(wires.size());
		for (ScreenPath& wire : wires)
		{
			mWirePaths.push_back(std::make_shared<const ScreenPath>(std::move(wire)));
		}
		mShapeChanged = true;
	}

	void PathShapeWidget::BuildShapes(std::vector<HitShape>& p_outShapes) const
	{
		RefreshShape();

		// One shape per wire: each has its own action, so the hover highlight and
		// the click report identify which wire was hit.
		for (size_t index = 0; index < mWirePaths.size(); ++index)
		{
			const std::shared_ptr<const ScreenPath>& path = mWirePaths[index];
			if (!path || path->IsEmpty())
			{
				continue;
			}
			const bool closed = !path->closed.empty() && path->closed[0];

			HitShape shape;
			shape.type = HitShape::EType::Path;
			shape.path = path;
			// A closed wire can be filled; an open polyline is only ever stroked.
			shape.pickMode = closed
				? HitShape::EPickMode::FillOrStroke
				: HitShape::EPickMode::Stroke;
			shape.pad = 2.0f;
			shape.strokeWidth = 2.0f;
			shape.strokePickSlack = 5.0f;
			shape.action = static_cast<int>(index) + 1;
			p_outShapes.push_back(shape);
		}
		// The shapes now match the cached wires again.
		mShapeChanged = false;
	}

	void PathShapeWidget::DrawContent(ImDrawList& p_drawList, const ScreenRect& p_rect)
	{
		// DrawContent() runs every frame (unlike BuildShapes(), which only runs
		// when the hit geometry is dirty), so this is where a source change is
		// noticed. The hit geometry catches up on the next frame.
		RefreshShape();
		if (mShapeChanged)
		{
			mShapeChanged = false;
			MarkGeometryDirty();
		}

		// A faint frame so it is obvious where the widget rectangle is while
		// testing; the shape itself is the part that reacts to the cursor.
		p_drawList.AddRect(
			p_rect.TopLeft(),
			ImVec2(p_rect.x + p_rect.w, p_rect.y + p_rect.h),
			IM_COL32(120, 130, 150, 70),
			4.0f,
			1.0f,
			0);

		DrawShapes(p_drawList, p_rect);
	}

	void PathShapeWidget::OnAction(int p_action)
	{
		const char* source = mSourceIsSketch ? "sketch" : "builtin";
		CORE_INFO(
			"[PathShape] clicked face {0} of {1} (source={2})",
			p_action,
			mWirePaths.size(),
			source);
	}
}
