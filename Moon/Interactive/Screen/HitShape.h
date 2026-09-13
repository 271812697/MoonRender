#pragma once
#include "Qtimgui/imgui/imgui.h"
#include <vector>

namespace MOON
{
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
			Polygon
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
