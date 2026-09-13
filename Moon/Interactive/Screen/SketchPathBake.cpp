#include "Interactive/Screen/SketchPathBake.h"
#include "Sketcher/SketcherObj.h"
#include "core/log.h"

#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRepBndLib.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GCPnts_QuasiUniformDeflection.hxx>
#include <GeomAdaptor_Curve.hxx>
#include <Geom_Curve.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Iterator.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Pnt.hxx>
#include <algorithm>
#include <cmath>

namespace MOON
{
	namespace
	{
		/** Points closer than this are treated as the same (sketch units).
		 * Coordinate storage is float, which is worth about 1e-5 on a 100mm
		 * sketch, so this only merges the joints between edges. */
		constexpr float kJointEpsilon = 1e-4f;
		float Distance(const ImVec2& p_a, const ImVec2& p_b)
		{
			const float dx = p_a.x - p_b.x;
			const float dy = p_a.y - p_b.y;
			return std::sqrt(dx * dx + dy * dy);
		}

		/** Appends the samples of one edge, dropping the point that coincides
		 * with the end of the previous edge. */
		void SampleEdge(
			const TopoDS_Edge& p_edge,
			double p_deflection,
			float p_jointTolerance,
			std::vector<ImVec2>& p_outPoints)
		{
			if (BRep_Tool::Degenerated(p_edge))
			{
				return;
			}

			// Sample the edge's own curve and ignore its placement.
			//
			// SketcherObj::toShape() ends with setTransform(planeTransform), which
			// hangs the sketch plane placement on the shape, and BRepAdaptor_Curve
			// applies exactly that placement. Taking (x, y) of those transformed
			// points would collapse a circle drawn off the XY plane into a line.
			// GeomCurve::toShape() builds the edge straight from the geometry's
			// curve, so the curve's own space *is* the sketch 2D space.
			TopLoc_Location location;
			Standard_Real firstParameter = 0.0;
			Standard_Real lastParameter = 0.0;
			Handle(Geom_Curve) curve = BRep_Tool::Curve(
				p_edge, location, firstParameter, lastParameter);
			if (curve.IsNull())
			{
				return;
			}

			GeomAdaptor_Curve adaptor(curve, firstParameter, lastParameter);
			GCPnts_QuasiUniformDeflection sampler(adaptor, p_deflection);
			if (!sampler.IsDone())
			{
				return;
			}

			// A wire stores its edges with an orientation, and two connected edges
			// share the vertex that sits at the end of one and the start of the
			// next. BRep_Tool::Curve() hands over the curve's own parameter range,
			// which does not follow that orientation, so a REVERSED edge has to be
			// appended backwards. Sampling it forwards jumps from the shared vertex
			// to the far end and walks back, which turns a straight edge into the
			// very same segment twice - the arc slot ended up as a self touching
			// outline exactly that way.
			const bool isReversed = p_edge.Orientation() == TopAbs_REVERSED;
			const int pointCount = sampler.NbPoints();
			for (int step = 0; step < pointCount; ++step)
			{
				const int index = isReversed ? (pointCount - step) : (step + 1);
				const gp_Pnt point = sampler.Value(index);
				const ImVec2 sample(
					static_cast<float>(point.X()),
					static_cast<float>(point.Y()));
				if (!p_outPoints.empty()
					&& Distance(p_outPoints.back(), sample) <= p_jointTolerance)
				{
					continue;
				}
				p_outPoints.push_back(sample);
			}
		}
	}

	std::vector<ScreenPath> BakeSketchFaces(
		SketcherObj& p_sketch,
		const SketchBakeOptions& p_options)
	{
		// The same topology makeDone() caches: toShape() chains the edges into
		// wires with OCCT, and Bullseye turns the closed wires into faces. Both are
		// recomputed here instead of reading doneWireShape / doneFaceShape, so a
		// widget baking while the sketch is still being edited never picks up a
		// stale result.
		Part::TopoShape wireShape = p_sketch.toShape();
		if (wireShape.isNull())
		{
			return {};
		}
		Part::TopoShape faceShape = wireShape.makeElementFace(nullptr, "Part::FaceMakerBullseye");
		if (faceShape.isNull())
		{
			return {};
		}

		// One shape per face: several closed wires that do not touch give several
		// faces, a wire drawn inside another one becomes a hole of the same face.
		std::vector<Part::TopoShape> faces = faceShape.getSubTopoShapes(TopAbs_FACE);
		if (faces.empty() && faceShape.getShape().ShapeType() == TopAbs_FACE)
		{
			faces.push_back(faceShape);
		}

		std::vector<ScreenPath> result;
		result.reserve(faces.size());
		for (Part::TopoShape& face : faces)
		{
			// The placement on the shape is ignored from here on: the loops are
			// sampled from the edges' own curves, which live in sketch 2D space.
			const TopoDS_Shape faceShape2 = face.getShape();
			if (faceShape2.ShapeType() != TopAbs_FACE)
			{
				continue;
			}
			const TopoDS_Face localFace = TopoDS::Face(faceShape2);

			// The face's outer wire first, then its holes; the even-odd rule does
			// not depend on that order, but keeping it makes the shape readable
			// when debugging.
			ScreenPath path;
			const TopoDS_Wire outerWire = BRepTools::OuterWire(localFace);
			std::vector<TopoDS_Wire> wires;
			wires.push_back(outerWire);
			for (TopoDS_Iterator it(localFace); it.More(); it.Next())
			{
				const TopoDS_Shape& child = it.Value();
				if (child.ShapeType() != TopAbs_WIRE)
				{
					continue;
				}
				const TopoDS_Wire wire = TopoDS::Wire(child);
				if (!wire.IsSame(outerWire))
				{
					wires.push_back(wire);
				}
			}

			for (const TopoDS_Wire& wire : wires)
			{
				std::vector<ImVec2> points;

				// Tolerances follow the size of the wire: an absolute value in
				// sketch units stops working as soon as the sketch is large (float
				// storage loses the low digits) and is needlessly tight on a small
				// one.
				Bnd_Box wireBounds;
				BRepBndLib::Add(wire, wireBounds);
				float wireExtent = 1.0f;
				if (!wireBounds.IsVoid())
				{
					Standard_Real xMin = 0.0, yMin = 0.0, zMin = 0.0;
					Standard_Real xMax = 0.0, yMax = 0.0, zMax = 0.0;
					wireBounds.Get(xMin, yMin, zMin, xMax, yMax, zMax);
					wireExtent = std::max(
						std::max(static_cast<float>(xMax - xMin),
							static_cast<float>(yMax - yMin)),
						1.0f);
				}
				const float jointTolerance = std::max(kJointEpsilon, 1e-5f * wireExtent);
				for (BRepTools_WireExplorer explorer(wire); explorer.More(); explorer.Next())
				{
					SampleEdge(
						explorer.Current(),
						p_options.flattenDeflection,
						jointTolerance,
						points);
				}
				if (points.size() < 3)
				{
					CORE_WARN(
						"[SketchBake] skipping a wire: only {0} sample points",
						points.size());
					continue;
				}

				// Verify that the loop closes instead of assuming it. ScreenPath
				// treats every loop as closed, so a bad join would silently be
				// papered over with a straight chord - that is how the arc slot
				// ended up as a self touching outline and refused to fill.
				const float closureTolerance = std::max(kJointEpsilon, 1e-4f * wireExtent);
				const bool flaggedClosed = BRep_Tool::IsClosed(wire);
				const float gap = Distance(points.front(), points.back());
				if (gap <= closureTolerance)
				{
					// Normal case: the last sample repeats the first one.
					points.pop_back();
				}
				else
				{
					CORE_WARN(
						"[SketchBake] wire loop does not close: gap {0:.4f} with {1} points "
						"(tolerance {2:.4f}); it is closed with a straight segment",
						gap,
						points.size(),
						closureTolerance);
				}
				if (!flaggedClosed)
				{
					CORE_WARN(
						"[SketchBake] OCCT does not flag this wire as closed "
						"({0} points, gap {1:.4f})",
						points.size(),
						gap);
				}
				if (points.size() < 3)
				{
					continue;
				}
				path.loops.push_back(std::move(points));
				path.closed.push_back(flaggedClosed || gap <= closureTolerance);
			}
			if (path.loops.empty())
			{
				continue;
			}

			path.RecomputeBounds();
			result.push_back(std::move(path));
		}
		return result;
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
