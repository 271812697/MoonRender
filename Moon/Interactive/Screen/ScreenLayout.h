#pragma once
#include "Qtimgui/imgui/imgui.h"

namespace MOON
{
	/** Rectangle in scene-view pixels with the origin at the top-left corner.
	 *
	 * This is the space of the mouse events, of FrameParam::cursor and of the
	 * ImGui overlay the screen widgets are drawn into, so layouts, hit tests and
	 * drawing all use it without any flip. Only the GL boundary (glViewport)
	 * converts to the bottom-left convention.
	 */
	struct ScreenRect
	{
		float x = 0.0f;
		float y = 0.0f;
		float w = 0.0f;
		float h = 0.0f;

		bool Contains(const ImVec2& p_point) const
		{
			return p_point.x >= x && p_point.x < x + w
				&& p_point.y >= y && p_point.y < y + h;
		}
		ImVec2 TopLeft() const { return ImVec2(x, y); }
		ImVec2 Center() const { return ImVec2(x + w * 0.5f, y + h * 0.5f); }
		ImVec2 Size() const { return ImVec2(w, h); }
		/** Widget local space: origin at the top-left corner. */
		ImVec2 ToLocal(const ImVec2& p_point) const
		{
			return ImVec2(p_point.x - x, p_point.y - y);
		}
		ImVec2 ToScreen(const ImVec2& p_local) const
		{
			return ImVec2(x + p_local.x, y + p_local.y);
		}
	};

	/** Where a screen widget sticks to when the viewport is resized. */
	enum class EScreenAnchor
	{
		TopLeft, TopCenter, TopRight,
		CenterLeft, Center, CenterRight,
		BottomLeft, BottomCenter, BottomRight
	};

	/** Declarative placement of a screen widget.
	 *
	 * All values are logical pixels. Resolve() turns anchor + offset + size into
	 * the concrete rectangle, which keeps the widgets free of their own anchor
	 * and flip arithmetic.
	 */
	struct ScreenLayout
	{
		EScreenAnchor anchor = EScreenAnchor::TopRight;
		/** Distance from the anchored corner/edge to the widget. */
		ImVec2 offset{ 0.0f, 0.0f };
		ImVec2 size{ 0.0f, 0.0f };
		/** UI scale applied on top of size and offset. */
		float scale = 1.0f;

		ScreenRect Resolve(int p_viewportWidth, int p_viewportHeight) const;
	};
}
