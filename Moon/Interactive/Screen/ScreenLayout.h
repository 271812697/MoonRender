#pragma once
#include <Rendering/Overlay/Im2D.h>

namespace MOON
{
	/** The 2D layer's own types.
	 *
	 * This directory builds, hit tests and describes screen shapes, and it must not have to
	 * pull the editor's UI library in to do it. Im2D is the drawing layer those shapes are
	 * drawn into, so its vector and its colour are the ones to carry them in; a vector is two
	 * floats either way, so nothing converts and nothing is copied at the boundary.
	 */
	using ScreenPoint = ::Rendering::Overlay::Im2D::Vec2;
	using ScreenColor = ::Rendering::Overlay::Im2D::ImU32;
	constexpr ScreenColor ScreenColorOf(int p_r, int p_g, int p_b, int p_a = 255) {
		return ::Rendering::Overlay::Im2D::ColorU32(p_r, p_g, p_b, p_a);
	}

	/** The cursor a widget asks for while the mouse is on one of its shapes.
	 *
	 * Deliberately its own enum: what a widget wants is 'a hand' or 'resize', and how that is
	 * expressed belongs to whichever windowing layer is in use. The overlay host translates
	 * it (see ImRenderer::drawWidgets). */
	enum class EScreenCursor
	{
		Arrow,
		TextInput,
		ResizeAll,
		ResizeNS,
		ResizeEW,
		ResizeNESW,
		ResizeNWSE,
		Hand,
		NotAllowed
	};

	/** Rectangle in scene-view pixels with the origin at the top-left corner.
	 *
	 * This is the space of the mouse events, of FrameParam::cursor and of the
	 * Im2D overlay the screen widgets are drawn into, so layouts, hit tests and
	 * drawing all use it without any flip. Only the GL boundary (glViewport)
	 * converts to the bottom-left convention.
	 */
	struct ScreenRect
	{
		float x = 0.0f;
		float y = 0.0f;
		float w = 0.0f;
		float h = 0.0f;

		bool Contains(const ScreenPoint& p_point) const
		{
			return p_point.x >= x && p_point.x < x + w
				&& p_point.y >= y && p_point.y < y + h;
		}
		ScreenPoint TopLeft() const { return ScreenPoint(x, y); }
		ScreenPoint Center() const { return ScreenPoint(x + w * 0.5f, y + h * 0.5f); }
		ScreenPoint Size() const { return ScreenPoint(w, h); }
		/** Widget local space: origin at the top-left corner. */
		ScreenPoint ToLocal(const ScreenPoint& p_point) const
		{
			return ScreenPoint(p_point.x - x, p_point.y - y);
		}
		ScreenPoint ToScreen(const ScreenPoint& p_local) const
		{
			return ScreenPoint(x + p_local.x, y + p_local.y);
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
		ScreenPoint offset{ 0.0f, 0.0f };
		ScreenPoint size{ 0.0f, 0.0f };
		/** UI scale applied on top of size and offset. */
		float scale = 1.0f;

		ScreenRect Resolve(int p_viewportWidth, int p_viewportHeight) const;
	};
}
