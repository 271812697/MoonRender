#pragma once
#include "Interactive/Screen/ShapeBuilder.h"

namespace MOON
{
	class SketcherObj;

	/** Options for turning a sketch into a widget shape. The flattening tolerance
	 * is the one every shape source shares, see ShapeBakeOptions. */
	using SketchBakeOptions = ShapeBakeOptions;

	/** Bakes the faces of a sketch into widget shapes.
	 *
	 * The topology comes from the sketch itself: SketcherObj::toShape() chains
	 * the edges into wires with OCCT - exactly what makeDone() caches as
	 * doneWireShape / doneFaceShape - and BakeShapeFaces() turns the wires into
	 * faces and then into outlines. A shape built by ShapeBuilder() instead of a
	 * sketch goes through the very same code.
	 *
	 * Output is in sketch coordinates (y up, z ignored). Finish with
	 * FitWiresInto() to map the faces into a widget rectangle, which also flips
	 * y into screen space.
	 */
	std::vector<ScreenPath> BakeSketchFaces(
		SketcherObj& p_sketch,
		const SketchBakeOptions& p_options = SketchBakeOptions());

	/** Maps a whole bake into p_rect with one shared transform.
	 *
	 * Fitting each wire on its own would pile them on top of each other; the
	 * shared transform keeps their relative positions, so two separate wires in
	 * the sketch stay two separate shapes in the widget.
	 */
	void FitWiresInto(std::vector<ScreenPath>& p_wires, const ScreenRect& p_rect, bool p_flipY);
}
