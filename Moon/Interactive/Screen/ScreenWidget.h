#pragma once
#include "Interactive/EventWidget.h"
#include "Interactive/Screen/HitShape.h"
#include "Interactive/Screen/ScreenLayout.h"
#include <vector>

namespace MOON
{
	/** Interaction states shared by every 2D widget.
	 *
	 * Stop     - cursor outside the widget.
	 * Hot      - cursor inside, nothing held down.
	 * Pressed  - left button went down on a shape; the action already fired and
	 *            the state latches until the release so one press cannot fire
	 *            twice.
	 * Dragging - the widget captured the cursor and keeps receiving it even when
	 *            it leaves the widget rectangle.
	 */
	enum class EScreenState
	{
		Stop,
		Hot,
		Pressed,
		Dragging
	};

	/** Styling used by DrawShapes() for the three visual states. */
	struct ScreenShapeStyle
	{
		ImU32 idleFill = IM_COL32(206, 216, 232, 170);
		ImU32 hotFill = IM_COL32(255, 255, 0, 235);
		ImU32 pressedFill = IM_COL32(255, 176, 40, 255);
		ImU32 outline = IM_COL32(24, 26, 30, 150);
		float outlineWidth = 1.0f;
		float hotOutlineWidth = 2.0f;
	};

	/** Base class for widgets drawn as a 2D overlay on the viewport.
	 *
	 * A 3D widget picks through the picking pass or a CPU ray; a screen widget is
	 * a pure overlay: it draws into an ImDrawList and resolves clicks with a 2D
	 * intersection test against the same shapes it draws. The parts that are easy
	 * to get subtly wrong - the cursor space, the anchor arithmetic, the
	 * hover/press/drag state machine, and telling the rest of the engine that the
	 * click is taken - live here once instead of in every widget.
	 */
	class ScreenWidget : public EventWidget
	{
	public:
		ScreenWidget(const std::string& name);
		virtual ~ScreenWidget();

		// Sealed: every screen widget refreshes the cursor and the state machine
		// the same way, so subclasses implement the hooks further down instead.
		void onUpdate() final;
		void onMouseMove() final;
		void onLeftMousePressed() final;
		void onLeftMouseReleased() final;

		/** True while this widget owns the cursor, either because the cursor is
		 * inside its rectangle or on one of its shapes, or because it captured
		 * the cursor for a drag. Scene picking and the navigation cube consult
		 * the overlay registry so they do not react to the same click. */
		bool BlocksSceneCursor(float p_x, float p_y) const;
		/** True when the cursor sits on a clickable shape rather than only on the
		 * widget rectangle. HUD style widgets give way to such a click. */
		bool HitsShape(float p_x, float p_y) const;
		bool IsCapturing() const { return mState == EScreenState::Dragging; }
		EScreenState GetState() const { return mState; }
		/** Higher order wins when several screen widgets overlap. */
		float GetZOrder() const { return mZOrder; }
		void SetZOrder(float p_zOrder);
		/** Widgets whose rectangle should stay click through can turn this off,
		 * leaving only their shapes as the pick area. */
		void SetRectBlocksCursor(bool p_block) { mRectBlocksCursor = p_block; }
		void SetUiScale(float p_scale);
		const ScreenRect& GetScreenRect() const;

	protected:
		/** Placement of the widget, resolved against the viewport every frame. */
		virtual ScreenLayout BuildLayout() const = 0;
		/** Clickable shapes, in widget local space. */
		virtual void BuildShapes(std::vector<HitShape>& p_outShapes) const {}
		/** Draw the content; p_rect is the resolved placement. */
		virtual void DrawContent(ImDrawList& p_drawList, const ScreenRect& p_rect) {}
		/** True for widgets that keep the cursor while the button is held down
		 * (sliders, handles). Plain buttons leave it false and fire on press. */
		virtual bool WantsCapture() const { return false; }
		/** A shape was clicked; p_action is HitShape::action. */
		virtual void OnAction(int p_action) {}
		/** Cursor moved while this widget holds a capture. */
		virtual void OnDrag(const ImVec2& p_localCursor) {}
		/** The widget just captured the cursor. */
		virtual void OnDragBegin(const ImVec2& p_localCursor) {}
		/** The capture ended, either on release or because the widget stopped
		 * being interactive (tool switch, disabled). This is where a widget
		 * commits or discards the value it was dragging. */
		virtual void OnDragEnd(const ImVec2& p_localCursor) {}
		/** State transition hook, for widgets that react to hover. */
		virtual void OnStateChanged(EScreenState p_state) {}
		/** Widgets can temporarily switch themselves off, e.g. while the camera
		 * is locked by the sketcher. */
		virtual bool IsInteractionEnabled() const { return true; }

		// Cursor and input, all in top-left viewport pixels. The interactor holds
		// the position of the event being dispatched, so the press callback sees
		// the exact press position instead of the previous frame's cursor.
		ImVec2 GetCursor() const;
		ImVec2 GetCursorLocal() const;
		/** Cursor movement of the current event, for drag handling. */
		ImVec2 GetFrameDelta() const;
		bool IsCursorInsideViewport() const;
		ImVec2 GetViewportSize() const;
		bool IsCtrlDown() const;
		bool IsShiftDown() const;
		bool IsAltDown() const;

		// Geometry helpers.
		const std::vector<HitShape>& GetShapes() const;
		/** Index of the shape under the cursor, -1 when none. */
		int HitTestShapes(const ImVec2& p_localCursor) const;
		int GetHotShape() const { return mHotShape; }
		/** Shape grabbed by the current press or drag (-1 when none).
		 *
		 * During a drag this stays on the grabbed shape even when the cursor
		 * leaves it, which is how a widget with several handles knows which one
		 * OnDrag() is moving. */
		int GetActiveShape() const { return mHotShape; }
		const HitShape* GetHotShapePtr() const;
		/** Rebuild the shapes on the next query. */
		void MarkGeometryDirty() { mGeometryDirty = true; }
		/** Cursor shown while hovering a shape (Arrow leaves it to others). */
		void SetHoverCursor(ImGuiMouseCursor p_cursor) { mHoverCursor = p_cursor; }
		/** Draw every shape with the style, highlighting the hovered one. */
		void DrawShapes(ImDrawList& p_drawList, const ScreenRect& p_rect);
		void SetShapeStyle(const ScreenShapeStyle& p_style) { mShapeStyle = p_style; }

	private:
		void EnsureGeometry() const;
		void RefreshState();
		void SetState(EScreenState p_state);
		bool IsInteractive() const;
		/** Shape under the cursor, honouring the widget z order. -1 when nothing
		 * should react. */
		int FindHotShape() const;
		/** Leaves EScreenState::Dragging, notifying OnDragEnd() first. */
		void EndDrag();

	private:
		mutable ScreenRect mRect;
		mutable std::vector<HitShape> mShapes;
		mutable bool mGeometryDirty = true;
		EScreenState mState = EScreenState::Stop;
		int mHotShape = -1;
		float mZOrder = 0.0f;
		float mUiScale = 1.0f;
		bool mRectBlocksCursor = true;
		ImGuiMouseCursor mHoverCursor = ImGuiMouseCursor_Arrow;
		ScreenShapeStyle mShapeStyle;
	};
}
