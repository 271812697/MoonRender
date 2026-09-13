#include "Interactive/Screen/SketchPathBake.h"
#include "Interactive/Screen/ShapeBuilder.h"
#include "Sketcher/SketcherObj.h"

#include <algorithm>

namespace MOON
{
	std::vector<ScreenPath> BakeSketchFaces(
		SketcherObj& p_sketch,
		const SketchBakeOptions& p_options)
	{
		// toShape() chains the sketch's edges into wires and hangs the plane
		// placement on the result. The placement is ignored by the bake - the
		// loops are sampled from the curves' own 2D space - so this is the same
		// face bake every other shape source goes through, see ShapeBuilder.h.
		//
		// It is deliberately recomputed instead of reading doneWireShape /
		// doneFaceShape, so a widget baking while the sketch is still being edited
		// never picks up a stale result.
		return BakeShapeFaces(p_sketch.toShape().getShape(), p_options);
	}

	void FitWiresInto(std::vector<ScreenPath>& p_wires, const ScreenRect& p_rect, bool p_flipY)
	{
		// One bounding box over every wire, so the relative layout of the sketch
		// survives the fit; fitting wire by wire would pile them all onto the
		// centre of the rectangle.
		bool boundsValid = false;
		ImVec2 boundsMin(0.0f, 0.0f);
		ImVec2 boundsMax(0.0f, 0.0f);
		for (const ScreenPath& wire : p_wires)
		{
			if (!wire.boundsValid)
			{
				continue;
			}
			if (!boundsValid)
			{
				boundsMin = wire.boundsMin;
				boundsMax = wire.boundsMax;
				boundsValid = true;
				continue;
			}
			boundsMin.x = std::min(boundsMin.x, wire.boundsMin.x);
			boundsMin.y = std::min(boundsMin.y, wire.boundsMin.y);
			boundsMax.x = std::max(boundsMax.x, wire.boundsMax.x);
			boundsMax.y = std::max(boundsMax.y, wire.boundsMax.y);
		}
		if (!boundsValid)
		{
			return;
		}

		const float sizeX = boundsMax.x - boundsMin.x;
		const float sizeY = boundsMax.y - boundsMin.y;
		const float scale = std::min(
			sizeX > 1e-6f ? p_rect.w / sizeX : 1.0f,
			sizeY > 1e-6f ? p_rect.h / sizeY : 1.0f);
		const ImVec2 shapeCenter(
			(boundsMin.x + boundsMax.x) * 0.5f,
			(boundsMin.y + boundsMax.y) * 0.5f);
		const ImVec2 targetCenter(p_rect.x + p_rect.w * 0.5f, p_rect.y + p_rect.h * 0.5f);
		const float flip = p_flipY ? -1.0f : 1.0f;

		for (ScreenPath& wire : p_wires)
		{
			for (std::vector<ImVec2>& loop : wire.loops)
			{
				for (ImVec2& point : loop)
				{
					point.x = targetCenter.x + (point.x - shapeCenter.x) * scale;
					point.y = targetCenter.y + (point.y - shapeCenter.y) * scale * flip;
				}
			}
			wire.RecomputeBounds();
		}
	}
}
