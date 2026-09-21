#include "Interactive/Screen/ScreenWidget.h"
#include "Interactive/Interactive/RenderWindowInteractor.h"
#include "Interactive/Screen/ScreenOverlayRegistry.h"
#include "Qtimgui/imgui/imgui.h"
#include <cmath>

namespace MOON
{
	ScreenWidget::ScreenWidget(const std::string& name) : EventWidget(name)
	{
		ScreenOverlayRegistry::Instance().Register(this);
	}

	ScreenWidget::~ScreenWidget()
	{
		ScreenOverlayRegistry::Instance().Unregister(this);
	}

	void ScreenWidget::SetZOrder(float p_zOrder)
	{
		mZOrder = p_zOrder;
	}

	void ScreenWidget::SetUiScale(float p_scale)
	{
		mUiScale = p_scale > 0.0f ? p_scale : 1.0f;
	}

	bool ScreenWidget::IsInteractive() const
	{
		// Deliberately free of any ImGui state: the interaction layer must not
		// depend on the library that currently happens to do the drawing.
		return isActived() && isVisible() && IsInteractionEnabled();
	}

	void ScreenWidget::EnsureGeometry() const
	{
		// The rectangle is cheap to resolve and changes with the viewport, so it
		// is recomputed on every query; the shapes only change when the widget
		// asks for it.
		int viewportSize[2] = { 0, 0 };
		if (Interactor != nullptr)
		{
			Interactor->GetSize(viewportSize);
		}

		ScreenLayout layout = BuildLayout();
		layout.scale *= mUiScale;
		mRect = layout.Resolve(viewportSize[0], viewportSize[1]);

		if (mGeometryDirty)
		{
			mShapes.clear();
			BuildShapes(mShapes);
			mGeometryDirty = false;
		}
	}

	ImVec2 ScreenWidget::GetCursor() const
	{
		if (Interactor == nullptr)
		{
			return ImVec2(0.0f, 0.0f);
		}
		// Top-left pixels: the same space as the layout and the ImGui overlay.
		const int* position = Interactor->GetEventPositionFlipY();
		return ImVec2(static_cast<float>(position[0]), static_cast<float>(position[1]));
	}

	ImVec2 ScreenWidget::GetCursorLocal() const
	{
		EnsureGeometry();
		return mRect.ToLocal(GetCursor());
	}

	ImVec2 ScreenWidget::GetFrameDelta() const
	{
		if (Interactor == nullptr)
		{
			return ImVec2(0.0f, 0.0f);
		}
		const int* current = Interactor->GetEventPositionFlipY();
		const int* previous = Interactor->GetLastEventPosition();
		int previousSize[2] = { 0, 0 };
		Interactor->GetSize(previousSize);
		// LastEventPosition keeps the bottom-left convention too.
		const float previousY = static_cast<float>(previousSize[1] - 1 - previous[1]);
		return ImVec2(
			static_cast<float>(current[0] - previous[0]),
			static_cast<float>(current[1] - previousY));
	}

	bool ScreenWidget::IsCursorInsideViewport() const
	{
		return Interactor != nullptr && Interactor->IsCursorInsideViewport();
	}

	ImVec2 ScreenWidget::GetViewportSize() const
	{
		int viewportSize[2] = { 0, 0 };
		if (Interactor != nullptr)
		{
			Interactor->GetSize(viewportSize);
		}
		return ImVec2(static_cast<float>(viewportSize[0]), static_cast<float>(viewportSize[1]));
	}

	bool ScreenWidget::IsCtrlDown() const
	{
		return Interactor != nullptr && Interactor->GetControlKey() != 0;
	}

	bool ScreenWidget::IsShiftDown() const
	{
		return Interactor != nullptr && Interactor->GetShiftKey() != 0;
	}

	bool ScreenWidget::IsAltDown() const
	{
		return Interactor != nullptr && Interactor->GetAltKey() != 0;
	}

	const ScreenRect& ScreenWidget::GetScreenRect() const
	{
		EnsureGeometry();
		return mRect;
	}

	const std::vector<HitShape>& ScreenWidget::GetShapes() const
	{
		EnsureGeometry();
		return mShapes;
	}

	int ScreenWidget::HitTestShapes(const ImVec2& p_localCursor) const
	{
		EnsureGeometry();
		// Back to front: the shapes drawn last are on top, so they must win when
		// two of them overlap.
		for (int i = static_cast<int>(mShapes.size()) - 1; i >= 0; --i)
		{
			if (mShapes[i].Contains(p_localCursor))
			{
				return i;
			}
		}
		return -1;
	}

	int ScreenWidget::FindHotShape() const
	{
		if (!IsInteractive() || !IsCursorInsideViewport())
		{
			return -1;
		}

		EnsureGeometry();
		const ImVec2 screenCursor = GetCursor();

		// Overlapping widgets: only the one on top may react. Widgets at the same
		// z order keep the old behaviour of both reacting.
		ScreenWidget* owner = ScreenOverlayRegistry::Instance().GetOwnerAt(
			screenCursor.x, screenCursor.y);
		if (owner != nullptr && owner != this && owner->GetZOrder() > GetZOrder())
		{
			return -1;
		}

		return HitTestShapes(mRect.ToLocal(screenCursor));
	}

	const HitShape* ScreenWidget::GetHotShapePtr() const
	{
		const std::vector<HitShape>& shapes = GetShapes();
		if (mHotShape < 0 || mHotShape >= static_cast<int>(shapes.size()))
		{
			return nullptr;
		}
		return &shapes[mHotShape];
	}

	bool ScreenWidget::BlocksSceneCursor(float p_x, float p_y) const
	{
		if (!IsInteractive())
		{
			return false;
		}
		if (mState == EScreenState::Dragging)
		{
			return true;
		}

		EnsureGeometry();
		const ImVec2 point(p_x, p_y);
		if (mRectBlocksCursor && mRect.Contains(point))
		{
			return true;
		}
		return HitTestShapes(mRect.ToLocal(point)) >= 0;
	}

	bool ScreenWidget::HitsShape(float p_x, float p_y) const
	{
		if (!IsInteractive())
		{
			return false;
		}
		if (mState == EScreenState::Dragging)
		{
			return true;
		}
		EnsureGeometry();
		return HitTestShapes(mRect.ToLocal(ImVec2(p_x, p_y))) >= 0;
	}

	void ScreenWidget::SetState(EScreenState p_state)
	{
		if (mState == p_state)
		{
			return;
		}
		mState = p_state;
		OnStateChanged(p_state);
	}

	void ScreenWidget::RefreshState()
	{
		EnsureGeometry();

		if (!IsInteractive())
		{
			// Losing interactivity in the middle of a drag (tool switch, widget
			// disabled, camera locked) must end the capture, otherwise the widget
			// would keep owning the cursor and, with it, keep the camera blocked.
			if (mState == EScreenState::Dragging)
			{
				EndDrag();
			}
			mHotShape = -1;
			SetState(EScreenState::Stop);
			return;
		}

		if (!IsCursorInsideViewport())
		{
			// A drag keeps the cursor even when it leaves the viewport, but a
			// hover must not stick around once the cursor is gone.
			if (mState != EScreenState::Dragging)
			{
				mHotShape = -1;
				SetState(EScreenState::Stop);
			}
			return;
		}

		const ImVec2 localCursor = mRect.ToLocal(GetCursor());

		if (mState == EScreenState::Dragging)
		{
			OnDrag(localCursor);
			return;
		}
		if (mState == EScreenState::Pressed)
		{
			// Latched until the release: the action already fired on press, the
			// hovered shape is only kept for the pressed highlight.
			return;
		}

		const int hot = FindHotShape();
		mHotShape = hot;
		SetState(hot >= 0 ? EScreenState::Hot : EScreenState::Stop);
	}

	void ScreenWidget::onUpdate()
	{
		if (!IsInteractive())
		{
			return;
		}

		ImDrawList* drawList = ImGui::GetForegroundDrawList();
		if (drawList == nullptr)
		{
			return;
		}

		// The only ImGui touch outside the draw calls themselves: the hovered
		// cursor shape. Kept here so the interaction code above stays free of the
		// library that will eventually be replaced.
		if (mHotShape >= 0 && mHoverCursor != ImGuiMouseCursor_Arrow)
		{
			ImGui::SetMouseCursor(mHoverCursor);
		}

		DrawContent(*drawList, mRect);
	}

	void ScreenWidget::onMouseMove()
	{
		RefreshState();
	}

	void ScreenWidget::onLeftMousePressed()
	{
		// "Click triggers the action": the work happens on press and the state
		// machine only latches so a single press cannot fire twice.
		RefreshState();
		// RefreshState() resolved the shape with the live cursor, the widget z
		// order and the ImGui capture state applied; a press that is already
		// latched or has nothing under it does nothing.
		if (mState != EScreenState::Hot || mHotShape < 0)
		{
			return;
		}
		const int hot = mHotShape;

		if (WantsCapture())
		{
			SetState(EScreenState::Dragging);
			OnDragBegin(GetCursorLocal());
			OnDrag(GetCursorLocal());
		}
		else
		{
			mHotShape = hot;
			SetState(EScreenState::Pressed);
			OnAction(mShapes[hot].action);
		}
	}

	void ScreenWidget::onLeftMouseReleased()
	{
		if (mState != EScreenState::Pressed && mState != EScreenState::Dragging)
		{
			return;
		}

		if (mState == EScreenState::Dragging)
		{
			EndDrag();
		}
		else
		{
			SetState(EScreenState::Stop);
		}
		// Re-evaluate where the release happened; the cursor may have moved off
		// the widget or out of the viewport while the button was held.
		RefreshState();
	}

	void ScreenWidget::EndDrag()
	{
		// Notify before leaving the state so the widget can still read it.
		OnDragEnd(GetCursorLocal());
		SetState(EScreenState::Stop);
	}

	void ScreenWidget::DrawShapes(ImDrawList& p_drawList, const ScreenRect& p_rect)
	{
		EnsureGeometry();
		const ImVec2 offset = p_rect.TopLeft();
		for (size_t i = 0; i < mShapes.size(); ++i)
		{
			const bool hot = static_cast<int>(i) == mHotShape;
			ImU32 fill = mShapeStyle.idleFill;
			float outlineWidth = mShapeStyle.outlineWidth;
			if (hot) {
				fill = (mState == EScreenState::Pressed)
					? mShapeStyle.pressedFill
					: mShapeStyle.hotFill;
				outlineWidth = mShapeStyle.hotOutlineWidth;
			}
			mShapes[i].Draw(&p_drawList, offset, fill, mShapeStyle.outline, outlineWidth);
		}
	}
}
