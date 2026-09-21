#include "Interactive/Screen/ShapeBuilder.h"
#include "core/log.h"
#include "TopoShape.h"

#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <GCPnts_QuasiUniformDeflection.hxx>
#include <GeomAdaptor_Curve.hxx>
#include <Geom_Curve.hxx>
#include <Precision.hxx>
#include <ShapeFix_Wire.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Iterator.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <algorithm>
#include <cmath>
#include <list>

namespace MOON
{
	namespace
	{
		constexpr double kPi = 3.14159265358979;
		constexpr double kDegToRad = kPi / 180.0;
		constexpr double kTwoPi = kPi * 2.0;

		/** Two points closer than this are meant to be the same one. The author
		 * places an arc by angle and the next curve by a point they computed
		 * themselves, so the two describe the same place but differ in the last
		 * digits of a float (~1e-5 on a 100px shape). OCCT's own tolerance is
		 * 1e-7, so without snapping the wire comes out disconnected. */
		constexpr double kSnapTolerance = 1e-3;

		double Distance(double p_ax, double p_ay, double p_bx, double p_by)
		{
			const double dx = p_ax - p_bx;
			const double dy = p_ay - p_by;
			return std::sqrt(dx * dx + dy * dy);
		}

		/** Point at an angle on the circle around (p_centerX, p_centerY). */
		gp_Pnt PointOnCircle(double p_centerX, double p_centerY, double p_radius, double p_angle)
		{
			return gp_Pnt(
				p_centerX + std::cos(p_angle) * p_radius,
				p_centerY + std::sin(p_angle) * p_radius,
				0.0);
		}

		/** Where an edge really ends: the built edge is authoritative, the point
		 * the author asked for is only a fallback. */
		bool EndPoint(const TopoDS_Edge& p_edge, double& p_outX, double& p_outY)
		{
			const TopoDS_Vertex vertex = TopExp::LastVertex(p_edge, Standard_True);
			if (vertex.IsNull())
			{
				return false;
			}
			const gp_Pnt point = BRep_Tool::Pnt(vertex);
			p_outX = point.X();
			p_outY = point.Y();
			return true;
		}

		/** Points closer than this are treated as the same, in the units of the
		 * shape. Coordinate storage is float, which is worth about 1e-5 on a
		 * 100mm sketch, so this only merges the joints between edges. */
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

		/** Turns one face into its outline: the outer loop first, then the holes. */
		bool FlattenFace(const TopoDS_Shape& p_faceShape, double p_deflection, ScreenPath& p_outPath)
		{
			if (p_faceShape.ShapeType() != TopAbs_FACE)
			{
				return false;
			}
			const TopoDS_Face localFace = TopoDS::Face(p_faceShape);

			// The face's outer wire first, then its holes; the even-odd rule does
			// not depend on that order, but keeping it makes the shape readable
			// when debugging.
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

				// Tolerances follow the size of the wire: an absolute value stops
				// working as soon as the shape is large (float storage loses the low
				// digits) and is needlessly tight on a small one.
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
					SampleEdge(explorer.Current(), p_deflection, jointTolerance, points);
				}
				if (points.size() < 3)
				{
					CORE_WARN(
						"[ShapeBake] skipping a wire: only {0} sample points",
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
						"[ShapeBake] wire loop does not close: gap {0:.4f} with {1} points "
						"(tolerance {2:.4f}); it is closed with a straight segment",
						gap,
						points.size(),
						closureTolerance);
				}
				if (!flaggedClosed)
				{
					CORE_WARN(
						"[ShapeBake] OCCT does not flag this wire as closed "
						"({0} points, gap {1:.4f})",
						points.size(),
						gap);
				}
				if (points.size() < 3)
				{
					continue;
				}
				p_outPath.loops.push_back(std::move(points));
				p_outPath.closed.push_back(flaggedClosed || gap <= closureTolerance);
			}
			if (p_outPath.loops.empty())
			{
				return false;
			}
			p_outPath.RecomputeBounds();
			return true;
		}

		/** The wires of p_shape, or null when it holds none. */
		TopoDS_Shape CollectWires(const TopoDS_Shape& p_shape)
		{
			std::vector<TopoDS_Wire> wires;
			for (TopExp_Explorer explorer(p_shape, TopAbs_WIRE); explorer.More(); explorer.Next())
			{
				wires.push_back(TopoDS::Wire(explorer.Current()));
			}
			if (wires.empty())
			{
				return TopoDS_Shape();
			}
			if (wires.size() == 1)
			{
				return wires.front();
			}
			BRep_Builder builder;
			TopoDS_Compound compound;
			builder.MakeCompound(compound);
			for (const TopoDS_Wire& wire : wires)
			{
				builder.Add(compound, wire);
			}
			return compound;
		}
	}

	TopoDS_Shape ConnectEdgesToWires(const std::vector<TopoDS_Edge>& p_edges)
	{
		std::list<TopoDS_Edge> remaining(p_edges.begin(), p_edges.end());
		std::vector<TopoDS_Wire> wires;

		while (!remaining.empty())
		{
			BRepBuilderAPI_MakeWire makeWire;
			// Add and erase the first edge: a wire grows from wherever it starts.
			makeWire.Add(remaining.front());
			remaining.pop_front();
			if (!makeWire.IsDone())
			{
				CORE_WARN("[ShapeBake] a free edge cannot start a wire");
				continue;
			}
			TopoDS_Wire wire = makeWire.Wire();

			// Try to connect every other edge to it; the loop restarts whenever
			// one fits, because the added edge changes where the wire ends.
			bool found = false;
			do
			{
				found = false;
				for (auto it = remaining.begin(); it != remaining.end(); ++it)
				{
					makeWire.Add(*it);
					if (makeWire.Error() != BRepBuilderAPI_DisconnectedWire)
					{
						// Edge added, so it is no longer a free edge.
						found = true;
						remaining.erase(it);
						wire = makeWire.Wire();
						break;
					}
				}
			} while (found);

			// Fix any topological issues of the wire.
			ShapeFix_Wire fix;
			fix.SetPrecision(Precision::Confusion());
			fix.Load(wire);
			fix.FixReorder();
			fix.FixConnected();
			fix.FixClosed();
			wires.push_back(fix.Wire());
		}

		if (wires.empty())
		{
			return TopoDS_Shape();
		}
		if (wires.size() == 1)
		{
			return wires.front();
		}
		BRep_Builder builder;
		TopoDS_Compound compound;
		builder.MakeCompound(compound);
		for (const TopoDS_Wire& wire : wires)
		{
			builder.Add(compound, wire);
		}
		return compound;
	}

	std::vector<ScreenPath> BakeShapeFaces(
		const TopoDS_Shape& p_shape,
		const ShapeBakeOptions& p_options)
	{
		if (p_shape.IsNull())
		{
			return {};
		}

		// Faces are what the flattening walks over; anything else is turned into
		// faces first, the same way a finished sketch does it.
		TopoDS_Shape faceShape = p_shape;
		if (!TopExp_Explorer(p_shape, TopAbs_FACE).More())
		{
			TopoDS_Shape wireShape = CollectWires(p_shape);
			if (wireShape.IsNull())
			{
				std::vector<TopoDS_Edge> edges;
				for (TopExp_Explorer explorer(p_shape, TopAbs_EDGE); explorer.More(); explorer.Next())
				{
					edges.push_back(TopoDS::Edge(explorer.Current()));
				}
				wireShape = ConnectEdgesToWires(edges);
			}
			if (wireShape.IsNull())
			{
				CORE_WARN("[ShapeBake] the shape holds no edge that could form a wire");
				return {};
			}

			// Bullseye decides which wires enclose one face, so a wire inside
			// another one becomes a hole instead of a second shape.
			Part::TopoShape wired(wireShape);
			Part::TopoShape faces = wired.makeElementFace(nullptr, "Part::FaceMakerBullseye");
			if (faces.isNull())
			{
				CORE_WARN("[ShapeBake] the wires do not enclose a face");
				return {};
			}
			faceShape = faces.getShape();
		}

		std::vector<ScreenPath> result;
		for (TopExp_Explorer explorer(faceShape, TopAbs_FACE); explorer.More(); explorer.Next())
		{
			// The placement on the shape is ignored from here on: the loops are
			// sampled from the edges' own curves.
			ScreenPath path;
			if (FlattenFace(explorer.Current(), p_options.flattenDeflection, path))
			{
				result.push_back(std::move(path));
			}
		}
		return result;
	}

	ShapeBuilder::SubPath& ShapeBuilder::CurrentSubPath(double p_x, double p_y)
	{
		if (mSubPaths.empty())
		{
			// A curve without a MoveTo is taken as the start of a sub path: the
			// author forgot, but the shape they described is still possible.
			CORE_WARN(
				"[ShapeBuilder] a curve was added before MoveTo; a sub path is "
				"started at ({0:.2f}, {1:.2f})",
				p_x,
				p_y);
			MoveTo(static_cast<float>(p_x), static_cast<float>(p_y));
		}
		return mSubPaths.back();
	}

	void ShapeBuilder::AddEdge(const TopoDS_Edge& p_edge, const char* p_kind)
	{
		if (p_edge.IsNull())
		{
			CORE_WARN("[ShapeBuilder] OCCT refused to build a {0}", p_kind);
			return;
		}
		if (mSubPaths.empty())
		{
			// A curve that was not opened by MoveTo(): the sub path starts where
			// the curve does.
			double x = 0.0;
			double y = 0.0;
			const TopoDS_Vertex first = TopExp::FirstVertex(p_edge, Standard_True);
			if (!first.IsNull())
			{
				const gp_Pnt point = BRep_Tool::Pnt(first);
				x = point.X();
				y = point.Y();
			}
			CurrentSubPath(x, y);
		}
		SubPath& subPath = mSubPaths.back();
		subPath.edges.push_back(p_edge);
		// Ask the edge where it ends instead of trusting the requested point: two
		// edges of one sub path only connect when they share the coordinate
		// exactly, and a curve built by OCCT does not land on the very last digit
		// of a point we computed ourselves.
		if (!EndPoint(p_edge, subPath.currentX, subPath.currentY))
		{
			CORE_WARN("[ShapeBuilder] a {0} came back without an end vertex", p_kind);
		}
	}

	void ShapeBuilder::MoveTo(float p_x, float p_y)
	{
		SubPath subPath;
		subPath.startX = p_x;
		subPath.startY = p_y;
		subPath.currentX = subPath.startX;
		subPath.currentY = subPath.startY;
		mSubPaths.push_back(std::move(subPath));
	}

	void ShapeBuilder::LineTo(float p_x, float p_y)
	{
		SubPath& subPath = CurrentSubPath(p_x, p_y);
		BRepBuilderAPI_MakeEdge makeEdge(
			gp_Pnt(subPath.currentX, subPath.currentY, 0.0),
			gp_Pnt(p_x, p_y, 0.0));
		if (!makeEdge.IsDone())
		{
			CORE_WARN("[ShapeBuilder] OCCT refused to build a line segment");
			return;
		}
		AddEdge(makeEdge.Edge(), "line segment");
	}

	void ShapeBuilder::ArcByCenter(
		float p_centerX,
		float p_centerY,
		float p_radius,
		float p_startDeg,
		float p_sweepDeg)
	{
		if (p_radius <= 0.0f)
		{
			CORE_WARN("[ShapeBuilder] an arc needs a positive radius");
			return;
		}
		double sweep = static_cast<double>(p_sweepDeg) * kDegToRad;
		// Keep the sweep inside one turn: a full turn is what Circle() is for.
		while (sweep > kTwoPi)
		{
			sweep -= kTwoPi;
		}
		while (sweep < -kTwoPi)
		{
			sweep += kTwoPi;
		}
		if (std::abs(sweep) < 1e-9)
		{
			CORE_WARN("[ShapeBuilder] an arc needs a non zero sweep");
			return;
		}

		const double start = static_cast<double>(p_startDeg) * kDegToRad;
		const double radius = static_cast<double>(p_radius);
		const double centerX = static_cast<double>(p_centerX);
		const double centerY = static_cast<double>(p_centerY);
		gp_Pnt begin = PointOnCircle(centerX, centerY, radius, start);
		const gp_Pnt end = PointOnCircle(centerX, centerY, radius, start + sweep);
		// The middle of the arc: with the two ends it describes the arc through
		// three points, which is how the arc is built below.
		const gp_Pnt middle = PointOnCircle(centerX, centerY, radius, start + sweep * 0.5);

		// Continue from where the previous curve ended when the two are the same
		// place: building an arc from three points lets its start be that exact
		// coordinate instead of the one OCCT would compute for the angle.
		if (!mSubPaths.empty())
		{
			const SubPath& subPath = mSubPaths.back();
			if (Distance(begin.X(), begin.Y(), subPath.currentX, subPath.currentY)
				<= kSnapTolerance)
			{
				begin = gp_Pnt(subPath.currentX, subPath.currentY, 0.0);
			}
		}

		GC_MakeArcOfCircle makeArc(begin, middle, end);
		if (!makeArc.IsDone())
		{
			CORE_WARN("[ShapeBuilder] OCCT refused to build an arc of circle");
			return;
		}
		AddEdge(BRepBuilderAPI_MakeEdge(makeArc.Value()), "arc of circle");
	}

	void ShapeBuilder::ArcThrough(
		float p_throughX,
		float p_throughY,
		float p_endX,
		float p_endY)
	{
		SubPath& subPath = CurrentSubPath(p_endX, p_endY);
		GC_MakeArcOfCircle makeArc(
			gp_Pnt(subPath.currentX, subPath.currentY, 0.0),
			gp_Pnt(p_throughX, p_throughY, 0.0),
			gp_Pnt(p_endX, p_endY, 0.0));
		if (!makeArc.IsDone())
		{
			CORE_WARN(
				"[ShapeBuilder] the three points do not describe an arc; a line "
				"segment is used instead");
			LineTo(p_endX, p_endY);
			return;
		}
		AddEdge(BRepBuilderAPI_MakeEdge(makeArc.Value()), "three point arc");
	}

	void ShapeBuilder::Circle(float p_centerX, float p_centerY, float p_radius)
	{
		if (p_radius <= 0.0f)
		{
			CORE_WARN("[ShapeBuilder] a circle needs a positive radius");
			return;
		}
		MoveTo(p_centerX + p_radius, p_centerY);
		const gp_Ax2 axis(gp_Pnt(p_centerX, p_centerY, 0.0), gp_Dir(0.0, 0.0, 1.0));
		BRepBuilderAPI_MakeEdge makeEdge(gp_Circ(axis, static_cast<double>(p_radius)));
		if (!makeEdge.IsDone())
		{
			CORE_WARN("[ShapeBuilder] OCCT refused to build a circle");
			return;
		}
		// A circle is closed, so the current point stays where MoveTo put it.
		AddEdge(makeEdge.Edge(), "circle");
	}

	void ShapeBuilder::ArcSlot(
		float p_centerX,
		float p_centerY,
		float p_radius,
		float p_halfWidth,
		float p_startDeg,
		float p_sweepDeg)
	{
		if (p_radius <= 0.0f || p_halfWidth <= 0.0f)
		{
			CORE_WARN("[ShapeBuilder] an arc slot needs a positive radius and width");
			return;
		}
		if (p_halfWidth >= p_radius)
		{
			CORE_WARN(
				"[ShapeBuilder] the arc slot is {0:.2f} wide but only {1:.2f} away "
				"from the center, so its inner edge would pass through the center",
				p_halfWidth * 2.0f,
				p_radius);
			return;
		}
		if (std::abs(p_sweepDeg) < 1e-3f || std::abs(p_sweepDeg) >= 360.0f)
		{
			CORE_WARN("[ShapeBuilder] an arc slot sweep has to stay inside one turn");
			return;
		}

		const float outerRadius = p_radius + p_halfWidth;
		const float innerRadius = p_radius - p_halfWidth;
		const float start = p_startDeg;
		const float end = p_startDeg + p_sweepDeg;
		// Positive sweeps walk counter clockwise, so the caps turn the same way
		// round their own center and the walk never doubles back on itself.
		const float capSweep = p_sweepDeg > 0.0f ? 180.0f : -180.0f;

		// The caps are half circles around the two ends of the center line; the
		// outer and inner edges meet them exactly there, so the outline closes.
		const auto onCenterLine = [p_centerX, p_centerY, p_radius](float p_angleDeg)
			{
				const float angle = static_cast<float>(p_angleDeg * kDegToRad);
				return ImVec2(
					p_centerX + std::cos(angle) * p_radius,
					p_centerY + std::sin(angle) * p_radius);
			};
		const ImVec2 startCap = onCenterLine(start);
		const ImVec2 endCap = onCenterLine(end);

		const float startAngle = static_cast<float>(start * kDegToRad);
		const ImVec2 begin(
			p_centerX + std::cos(startAngle) * outerRadius,
			p_centerY + std::sin(startAngle) * outerRadius);

		MoveTo(begin.x, begin.y);
		// Outer edge, around the far end, back along the inner edge, then around
		// the near end: one closed sub path.
		ArcByCenter(p_centerX, p_centerY, outerRadius, start, p_sweepDeg);
		ArcByCenter(endCap.x, endCap.y, p_halfWidth, end, capSweep);
		ArcByCenter(p_centerX, p_centerY, innerRadius, end, -p_sweepDeg);
		ArcByCenter(startCap.x, startCap.y, p_halfWidth, start + 180.0f, capSweep);
		Close();
	}

	void ShapeBuilder::Close()
	{
		if (mSubPaths.empty())
		{
			return;
		}
		SubPath& subPath = mSubPaths.back();
		if (subPath.edges.empty())
		{
			return;
		}
		// Any gap bigger than what OCCT already treats as one point needs a real
		// edge: the two ends were authored separately, so "nearly closed" is not
		// closed. Below that they share a vertex as far as the wire is concerned.
		if (Distance(subPath.currentX, subPath.currentY, subPath.startX, subPath.startY)
			> Precision::Confusion())
		{
			BRepBuilderAPI_MakeEdge makeEdge(
				gp_Pnt(subPath.currentX, subPath.currentY, 0.0),
				gp_Pnt(subPath.startX, subPath.startY, 0.0));
			if (!makeEdge.IsDone())
			{
				CORE_WARN("[ShapeBuilder] OCCT refused to close the sub path");
				return;
			}
			subPath.edges.push_back(makeEdge.Edge());
			subPath.currentX = subPath.startX;
			subPath.currentY = subPath.startY;
		}
	}

	void ShapeBuilder::Clear()
	{
		mSubPaths.clear();
	}

	TopoDS_Shape ShapeBuilder::BuildWires() const
	{
		std::vector<TopoDS_Wire> wires;
		for (const SubPath& subPath : mSubPaths)
		{
			if (subPath.edges.empty())
			{
				continue;
			}
			// The edges were authored in walk order and share their endpoints, so
			// they can simply be added one after another.
			BRepBuilderAPI_MakeWire makeWire;
			for (const TopoDS_Edge& edge : subPath.edges)
			{
				makeWire.Add(edge);
			}
			if (!makeWire.IsDone())
			{
				CORE_WARN(
					"[ShapeBuilder] a sub path with {0} curves does not connect "
					"(error {1}); the rest of the shape is kept",
					subPath.edges.size(),
					static_cast<int>(makeWire.Error()));
				continue;
			}

			ShapeFix_Wire fix;
			fix.SetPrecision(Precision::Confusion());
			fix.Load(makeWire.Wire());
			fix.FixReorder();
			fix.FixConnected();
			fix.FixClosed();
			wires.push_back(fix.Wire());
		}

		if (wires.empty())
		{
			return TopoDS_Shape();
		}
		if (wires.size() == 1)
		{
			return wires.front();
		}
		BRep_Builder builder;
		TopoDS_Compound compound;
		builder.MakeCompound(compound);
		for (const TopoDS_Wire& wire : wires)
		{
			builder.Add(compound, wire);
		}
		return compound;
	}

	TopoDS_Shape ShapeBuilder::BuildFaces() const
	{
		const TopoDS_Shape wires = BuildWires();
		if (wires.IsNull())
		{
			return TopoDS_Shape();
		}
		Part::TopoShape wired(wires);
		Part::TopoShape faces = wired.makeElementFace(nullptr, "Part::FaceMakerBullseye");
		if (faces.isNull())
		{
			CORE_WARN("[ShapeBuilder] the curves do not enclose a face");
			return TopoDS_Shape();
		}
		return faces.getShape();
	}

	std::vector<ScreenPath> ShapeBuilder::BuildPaths(const ShapeBakeOptions& p_options) const
	{
		return BakeShapeFaces(BuildFaces(), p_options);
	}
}
