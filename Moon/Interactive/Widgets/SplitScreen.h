#pragma once
#include "Interactive/Screen/ScreenWidget.h"

namespace MOON
{
	/** Draggable divider used by the path tracer to split the viewport.
	 *
	 * Two endpoint handles define the line and a middle handle moves the whole
	 * line. Only the handles are clickable - the widget leaves the rest of the
	 * viewport to the scene - and dragging one captures the cursor, so the handle
	 * keeps following it even when the cursor leaves the handle itself.
	 */
	class SplitScreen : public ScreenWidget
	{
	public:
		SplitScreen(const std::string& name);
		virtual ~SplitScreen();

		/** Plane equation of the divider in GL screen space (origin bottom-left),
		 * as (n.x, n.y, -(a . n)): the same convention the path tracer shader
		 * expects. The handles are stored in top-left pixels, so the y axis is
		 * flipped here. */
		void getLineEquation(float* out);

	protected:
		ScreenLayout BuildLayout() const override;
		void BuildShapes(std::vector<HitShape>& p_outShapes) const override;
		void DrawContent(ImDrawList& p_drawList, const ScreenRect& p_rect) override;
		bool WantsCapture() const override { return true; }
		void OnDragBegin(const ImVec2& p_localCursor) override;
		void OnDrag(const ImVec2& p_localCursor) override;
		void OnDragEnd(const ImVec2& p_localCursor) override;

	private:
		enum class EHandle
		{
			Start = 0,
			End = 1,
			Middle = 2
		};

		ImVec2 Middle() const;
		ImVec2 ClampToViewport(const ImVec2& p_local) const;
		void MoveHandle(int p_handle, const ImVec2& p_localCursor);

	private:
		/** Handles, in top-left screen pixels (the widget rectangle starts at the
		 * viewport origin, so local and screen coordinates are the same). */
		ImVec2 mStart{ 0.0f, 0.0f };
		ImVec2 mEnd{ 0.0f, 100.0f };
		/** Grab offset between the handle and the cursor when the drag started. */
		ImVec2 mGrabOffset{ 0.0f, 0.0f };
		/** Half vector of the line captured when the middle handle was grabbed;
		 * the middle moves the line rigidly instead of re-scaling it. */
		ImVec2 mHalfVector{ 0.0f, 0.0f };
		int mDraggedHandle = -1;
	};
}
