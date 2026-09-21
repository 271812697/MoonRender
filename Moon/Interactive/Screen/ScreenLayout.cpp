#include "Interactive/Screen/ScreenLayout.h"

namespace MOON
{
	namespace
	{
		/** Horizontal placement of the anchored pair (start, center, end). */
		float PlaceAxis(
			float p_viewport,
			float p_size,
			float p_offset,
			int p_anchorIndex)
		{
			switch (p_anchorIndex)
			{
			case 0: return p_offset;                          // start
			case 1: return (p_viewport - p_size) * 0.5f + p_offset; // center
			default: return p_viewport - p_size - p_offset;    // end
			}
		}
	}

	ScreenRect ScreenLayout::Resolve(int p_viewportWidth, int p_viewportHeight) const
	{
		const float viewportW = static_cast<float>(p_viewportWidth);
		const float viewportH = static_cast<float>(p_viewportHeight);
		const float width = size.x * scale;
		const float height = size.y * scale;
		const float offsetX = offset.x * scale;
		const float offsetY = offset.y * scale;

		// 0/1/2 per axis: top-left, center, bottom-right alignment.
		const int row = static_cast<int>(anchor) / 3;
		const int column = static_cast<int>(anchor) % 3;

		ScreenRect rect;
		rect.w = width;
		rect.h = height;
		rect.x = PlaceAxis(viewportW, width, offsetX, column);
		rect.y = PlaceAxis(viewportH, height, offsetY, row);
		return rect;
	}
}
