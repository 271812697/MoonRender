#pragma once
#include <cstdint>
#include <ThirdParty/imgui/imgui/imgui.h>

namespace Rendering::Context {
	class Driver;
}

namespace Rendering::Overlay
{
	/** A 2D draw list drawn straight into the frame, in screen space.
	 *
	 * It is ImGui's draw list without ImGui's windows: the editor draws its own overlay geometry
	 * (handles, guides, selection boxes) through this. The drawing is GL + the render module's
	 * driver - no window toolkit - so it belongs here with the rest of the overlay drawing.
	 *
	 * NewFrame() opens the list, GetDrawList() is what callers append to, EndFrame() uploads and
	 * draws it. Both must run with the render module's context current, inside its frame. */
	class Im2DRender
	{
	public:
		static Im2DRender& Instance();

		ImDrawList* GetDrawList();

		void NewFrame();
		/** Draws what was collected. p_viewWidth/Height are the view's size in pixels: the
		 * projection is built from them, screen space to clip space. */
		void EndFrame(::Rendering::Context::Driver& p_driver, uint16_t p_viewWidth, uint16_t p_viewHeight);

	private:
		Im2DRender();
		~Im2DRender();
		Im2DRender(const Im2DRender&) = delete;
		Im2DRender& operator=(const Im2DRender&) = delete;

		class Internal;
		Internal* mInternal = nullptr;
	};
}
