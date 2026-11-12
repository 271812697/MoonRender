#include "Interactive/Widgets/SplitScreen.h"
#include "Interactive/Screen/ScreenOverlayRegistry.h"
#include <cmath>

namespace MOON
{
	namespace
	{
		/** Drawn handle radius; the pick area is a couple of pixels wider so the
		 * handles stay comfortable to grab. */
		constexpr float kHandleRadius = 8.0f;
		constexpr float kHandleHitPadding = 2.0f;

		constexpr ScreenColor kLineColor = ScreenColorOf(255, 255, 0, 255);
	}

	SplitScreen::SplitScreen(const std::string& name) : ScreenWidget(name)
	{
		// The handles are the only clickable part: the widget rectangle covers the
		// whole viewport (local and screen coordinates are then the same), but it
		// must not swallow the scene picking around the handles.
		SetRectBlocksCursor(false);

		ScreenShapeStyle style;
		style.idleFill = ScreenColorOf(0, 235, 255, 255);
		style.hotFill = ScreenColorOf(255, 255, 0, 255);
		style.pressedFill = ScreenColorOf(255, 176, 40, 255);
		style.outline = ScreenColorOf(20, 22, 26, 160);
		SetShapeStyle(style);
	}

	SplitScreen::~SplitScreen()
	{
	}

	ScreenLayout SplitScreen::BuildLayout() const
	{
		// Anchored to the viewport origin with the viewport size, so the widget
		// local space equals the top-left screen space the handles are stored in.
		ScreenLayout layout;
		layout.anchor = EScreenAnchor::TopLeft;
		layout.offset = ScreenPoint(0.0f, 0.0f);
		layout.size = GetViewportSize();
		return layout;
	}

	ScreenPoint SplitScreen::Middle() const
	{
		return ScreenPoint((mStart.x + mEnd.x) * 0.5f, (mStart.y + mEnd.y) * 0.5f);
	}

	ScreenPoint SplitScreen::ClampToViewport(const ScreenPoint& p_local) const
	{
		const ScreenPoint viewport = GetViewportSize();
		return ScreenPoint(
			p_local.x < 0.0f ? 0.0f : (p_local.x > viewport.x ? viewport.x : p_local.x),
			p_local.y < 0.0f ? 0.0f : (p_local.y > viewport.y ? viewport.y : p_local.y));
	}

	void SplitScreen::BuildShapes(std::vector<HitShape>& p_outShapes) const
	{
		HitShape handle;
		handle.type = HitShape::EType::Circle;
		handle.radius = kHandleRadius;
		handle.pad = kHandleHitPadding;

		handle.action = static_cast<int>(EHandle::Start);
		handle.center = mStart;
		p_outShapes.push_back(handle);

		handle.action = static_cast<int>(EHandle::End);
		handle.center = mEnd;
		p_outShapes.push_back(handle);

		handle.action = static_cast<int>(EHandle::Middle);
		handle.center = Middle();
		p_outShapes.push_back(handle);
	}

	void SplitScreen::DrawContent(::Rendering::Overlay::Im2D::DrawList& p_drawList, const ScreenRect& p_rect)
	{
		// The line is decoration; the handles are the interactive shapes and are
		// drawn from the same geometry that is hit tested.
		p_drawList.AddLine(
			p_rect.ToScreen(mStart),
			p_rect.ToScreen(mEnd),
			kLineColor,
			1.0f);
		DrawShapes(p_drawList, p_rect);
	}

	void SplitScreen::OnDragBegin(const ScreenPoint& p_localCursor)
	{
		mDraggedHandle = GetActiveShape();

		ScreenPoint handlePosition = mStart;
		if (mDraggedHandle == static_cast<int>(EHandle::End))
		{
			handlePosition = mEnd;
		}
		else if (mDraggedHandle == static_cast<int>(EHandle::Middle))
		{
			handlePosition = Middle();
			// Rigid line move: remember the current half vector so the endpoints
			// keep their distance while the middle handle is dragged.
			mHalfVector = ScreenPoint(Middle().x - mStart.x, Middle().y - mStart.y);
		}

		mGrabOffset = ScreenPoint(
			handlePosition.x - p_localCursor.x,
			handlePosition.y - p_localCursor.y);
	}

	void SplitScreen::MoveHandle(int p_handle, const ScreenPoint& p_localCursor)
	{
		const ScreenPoint target = ClampToViewport(ScreenPoint(
			p_localCursor.x + mGrabOffset.x,
			p_localCursor.y + mGrabOffset.y));

		switch (static_cast<EHandle>(p_handle))
		{
		case EHandle::Start:
			mStart = target;
			break;
		case EHandle::End:
			mEnd = target;
			break;
		case EHandle::Middle:
			mStart = ScreenPoint(target.x - mHalfVector.x, target.y - mHalfVector.y);
			mEnd = ScreenPoint(target.x + mHalfVector.x, target.y + mHalfVector.y);
			break;
		default:
			break;
		}

		// The handles moved, so the hit shapes have to be rebuilt.
		MarkGeometryDirty();
	}

	void SplitScreen::OnDrag(const ScreenPoint& p_localCursor)
	{
		if (mDraggedHandle < 0)
		{
			return;
		}
		MoveHandle(mDraggedHandle, p_localCursor);
	}

	void SplitScreen::OnDragEnd(const ScreenPoint& p_localCursor)
	{
		if (mDraggedHandle >= 0)
		{
			// Apply the last position too: the release event carries the final
			// cursor, and the line equation below is read every frame by the path
			// tracer.
			MoveHandle(mDraggedHandle, p_localCursor);
		}
		mDraggedHandle = -1;
	}

	void SplitScreen::getLineEquation(float* out)
	{
		// The shader works in GL screen space, the handles in top-left pixels.
		const float viewportHeight = GetViewportSize().y;
		const ScreenPoint a(mStart.x, viewportHeight - mStart.y);
		const ScreenPoint b(mEnd.x, viewportHeight - mEnd.y);

		ScreenPoint direction(b.x - a.x, b.y - a.y);
		const float length = std::sqrt(
			direction.x * direction.x + direction.y * direction.y);
		if (length > 0.0001f)
		{
			direction = ScreenPoint(direction.x / length, direction.y / length);
		}
		else
		{
			// Degenerate line: keep a valid (vertical) plane equation instead of
			// producing NaNs in the shader.
			direction = ScreenPoint(0.0f, 1.0f);
		}

		const ScreenPoint normal(direction.y, -direction.x);
		out[0] = normal.x;
		out[1] = normal.y;
		out[2] = -(a.x * normal.x + a.y * normal.y);
	}
}
