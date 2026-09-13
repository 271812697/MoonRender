#pragma once
#include "Qtimgui/imgui/imgui.h"
#include <memory>
#include <vector>

namespace MOON
{
	struct ScreenPath;

	/** A 2D shape that is both drawn and hit tested.
	 *
	 * Drawing and hit testing share one description on purpose: a button drawn
	 * as a triangle must not be picked against a different approximation, or the
	 * clickable area silently drifts away from what the user sees. Points are in
	 * widget local space (origin at the widget rectangle top-left corner).
	 */
	struct HitShape
	{
		enum class EType
		{
			Rect,
			Circle,
			/** Band between two radii, used for arc buttons. */
			RingArc,
			/** Convex, filled. */
			Triangle,
			/** Convex, filled. */
			Polygon,
			/** An arbitrary outline, e.g. baked from a sketch. See ScreenPath. */
			Path
		};

		/** How a shape decides that the cursor is on it. */
		enum class EPickMode
		{
			/** Inside the outline. */
			Fill,
			/** Within strokePickSlack of the outline (open or closed). */
			Stroke,
			/** Either of the two. */
			FillOrStroke
		};

		EType type = EType::Rect;
		/** Semantic id reported by ScreenWidget::HitTestShapes(). */
		int action = 0;

		/** Rect: center. Circle / RingArc: circle center. */
		ImVec2 center{ 0.0f, 0.0f };
		/** Rect: half size. */
		ImVec2 halfExtent{ 0.0f, 0.0f };
		/** Circle / RingArc: radius of the center line of the band. */
		float radius = 0.0f;
		/** RingArc: half thickness of the band. */
		float bandHalfWidth = 0.0f;
		/** RingArc: start angle and sweep in degrees, screen space (y down). */
		float startAngleDeg = 0.0f;
		float sweepAngleDeg = 0.0f;
		/** Triangle / Polygon: convex outline in local space. */
		std::vector<ImVec2> points;

		/** Path: the outline. Shared between widgets that use the same shape. */
		std::shared_ptr<const ScreenPath> path;
		EPickMode pickMode = EPickMode::Fill;
		/** Path: drawn stroke thickness (Stroke mode). */
		float strokeWidth = 2.0f;
		/** Path: extra pick area around the stroke, in pixels. */
		float strokePickSlack = 4.0f;

		/** Extra pick area in pixels, applied uniformly to every shape type. */
		float pad = 0.0f;

		bool Contains(const ImVec2& p_local) const;
		void Draw(
			ImDrawList* p_drawList,
			const ImVec2& p_offset,
			ImU32 p_fill,
			ImU32 p_outline,
			float p_outlineWidth) const;
		/** Center of the shape in local space; used for pads and labels. */
		ImVec2 LocalCenter() const;
	};
}
