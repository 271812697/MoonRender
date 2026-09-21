#pragma once
#include "Interactive/Screen/ScreenPath.h"

#include <TopoDS_Edge.hxx>
#include <TopoDS_Shape.hxx>
#include <vector>

namespace MOON
{
	/** Options for turning OCCT curves into widget outlines. */
	struct ShapeBakeOptions
	{
		/** Chordal tolerance used when flattening a curve, in the curve's own
		 * units. A shape authored in widget pixels keeps a sub pixel error with
		 * the default; a sketch drawn in millimetres uses the same number in mm. */
		double flattenDeflection = 0.02;
	};

	/** Connects edges into wires by their shared endpoints, whatever order they
	 * are given in. The result is a wire, or a compound of wires when the edges
	 * form several of them, or a null shape when nothing could be connected.
	 *
	 * Same walk FreeCAD uses for a finished sketch (see SketcherObj::toShape):
	 * start a wire from one edge and keep appending whatever fits.
	 */
	TopoDS_Shape ConnectEdgesToWires(const std::vector<TopoDS_Edge>& p_edges);

	/** Turns OCCT curves into widget outlines: wires -> faces -> flattened loops.
	 *
	 * The topology comes from OCCT, exactly like a finished sketch:
	 * makeElementFace(..., Bullseye) decides which wires enclose one face, so a
	 * wire drawn inside another one becomes a hole of that face. One face
	 * becomes one ScreenPath, holding its outer loop followed by its hole loops
	 * - the shape the even-odd fill and hit test of ScreenPath expect.
	 *
	 * p_shape may hold faces, wires or loose edges; anything without a face is
	 * turned into faces first. The points come out in the shape's own 2D
	 * coordinates: nothing is scaled, fitted or flipped here, so the caller
	 * decides what one unit means.
	 */
	std::vector<ScreenPath> BakeShapeFaces(
		const TopoDS_Shape& p_shape,
		const ShapeBakeOptions& p_options = ShapeBakeOptions());

	/** Authors 2D widget shapes out of OCCT curves.
	 *
	 * Everything is built in the space the widget draws in, so the numbers that
	 * go in are the pixels that come out: for a screen overlay that is x right,
	 * y down, the origin wherever the widget puts it.
	 *
	 *     ShapeBuilder builder;
	 *     builder.MoveTo(0.0f, 0.0f);
	 *     builder.LineTo(40.0f, 0.0f);
	 *     builder.ArcByCenter(20.0f, 0.0f, 20.0f, 0.0f, 180.0f);
	 *     builder.Close();
	 *     std::vector<ScreenPath> paths = builder.BuildPaths();
	 *
	 * Every sub path becomes one wire. Two sub paths in the same builder share a
	 * face when one encloses the other, which is how a hole is made; two sub
	 * paths next to each other give two shapes.
	 */
	class ShapeBuilder
	{
	public:
		/** Starts a new sub path at p_x / p_y. */
		void MoveTo(float p_x, float p_y);
		/** Straight segment from the current point to p_x / p_y. */
		void LineTo(float p_x, float p_y);
		/** Arc of the circle around p_centerX / p_centerY with p_radius, from
		 * p_startDeg walking p_sweepDeg degrees. Angles are degrees counter
		 * clockwise in the XY plane, a negative sweep walks clockwise.
		 *
		 * The arc starts at the point at p_startDeg, which becomes the current
		 * point - so an arc is placed by the circle it walks on, not by where the
		 * previous curve ended. */
		void ArcByCenter(
			float p_centerX,
			float p_centerY,
			float p_radius,
			float p_startDeg,
			float p_sweepDeg);
		/** Arc from the current point, through p_throughX / p_throughY, to
		 * p_endX / p_endY (three point arc). */
		void ArcThrough(
			float p_throughX,
			float p_throughY,
			float p_endX,
			float p_endY);
		/** Full circle around p_centerX / p_centerY. A circle is already closed,
		 * so it is a sub path of its own. */
		void Circle(float p_centerX, float p_centerY, float p_radius);
		/** Arc slot: a band of constant width that follows a circle, closed with
		 * a rounded cap at each end.
		 *
		 * The slot's center line is p_radius away from p_centerX / p_centerY and
		 * covers p_sweepDeg degrees from p_startDeg; p_halfWidth is half its
		 * width. The whole slot is one sub path, so it comes out as one shape.
		 * A slot whose caps would meet (|sweep| + the caps >= 360 degrees) is not
		 * one slot any more; use Circle() or two slots for that.
		 */
		void ArcSlot(
			float p_centerX,
			float p_centerY,
			float p_radius,
			float p_halfWidth,
			float p_startDeg,
			float p_sweepDeg);
		/** Closes the current sub path back to its MoveTo point. */
		void Close();

		/** Drops every curve so the builder can be reused. */
		void Clear();

		/** The sub paths as wires, one wire per sub path. */
		TopoDS_Shape BuildWires() const;
		/** The wires turned into faces, see BakeShapeFaces. */
		TopoDS_Shape BuildFaces() const;
		/** The faces flattened into widget outlines. Empty when the curves cannot
		 * enclose a face, e.g. a single open sub path. */
		std::vector<ScreenPath> BuildPaths(
			const ShapeBakeOptions& p_options = ShapeBakeOptions()) const;

	private:
		/** One MoveTo...Close run. The edges are kept in walk order, so building
		 * the wire is just adding them one by one. */
		struct SubPath
		{
			std::vector<TopoDS_Edge> edges;
			/** Endpoints of the walk, kept in double. Every curve of a sub path
			 * has to share them bit for bit, or OCCT reports the wire as
			 * disconnected - see the note on AddEdge() in the .cpp. */
			double startX = 0.0;
			double startY = 0.0;
			double currentX = 0.0;
			double currentY = 0.0;
		};

		SubPath& CurrentSubPath(double p_x, double p_y);
		/** Takes an edge into the current sub path and remembers where it really
		 * ends, asking OCCT rather than trusting the requested point. */
		void AddEdge(const TopoDS_Edge& p_edge, const char* p_kind);

	private:
		std::vector<SubPath> mSubPaths;
	};
}
