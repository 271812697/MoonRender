#pragma once
#include <cstdint>

#include <Rendering/Overlay/Im2D.h>
#include <Rendering/Overlay/Im2DFont.h>

namespace Rendering::Context {
	class Driver;
}

namespace Rendering::Overlay
{
	/** The GL side of Im2D: uploads one frame of screen-space geometry and draws it.
	 *
	 * This used to hand ImGui's ImDrawList to a shader; it now owns the draw list and the font
	 * (see Im2D.h). No ImGui context, no ImGui draw data, no window layer: NewFrame() opens the
	 * list, callers append to it through GetDrawList(), EndFrame() uploads and draws it. Both
	 * calls must run with the render module's context current, inside its frame. */
	class Im2DRender
	{
	public:
		static Im2DRender& Instance();

		Im2D::DrawList& GetDrawList();
		Im2D::Font& GetFont();

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
