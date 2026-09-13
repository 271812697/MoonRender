#pragma once
#include "Interactive/Screen/ScreenPath.h"

namespace MOON
{
	class SketcherObj;

	/** Options for turning a sketch into a widget shape. */
	struct SketchBakeOptions
	{
		/** Chordal tolerance used when flattening the wires, in sketch units.
		 * The sampling is deflection based, so a large arc and a small one both
		 * end up with as many points as they need. */
		double flattenDeflection = 0.02;
	};

	/** Bakes the faces of a sketch into widget shapes.
	 *
	 * The topology comes from the sketch itself: SketcherObj::toShape() chains
	 * the edges into wires with OCCT, and makeElementFace(..., Bullseye) turns
	 * the closed wires into faces - exactly what makeDone() caches as
	 * doneWireShape / doneFaceShape. One face becomes one widget shape, so a
	 * sketch with two separate closed wires gives two shapes, and a wire drawn
	 * inside another one becomes a hole of that face.
	 *
	 * A returned path holds the face's outer loop followed by its hole loops, so
	 * the even-odd rule already used by the hit test describes the face exactly.
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
