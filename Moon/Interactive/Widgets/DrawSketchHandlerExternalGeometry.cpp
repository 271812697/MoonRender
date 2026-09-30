#include "Interactive/Widgets/DrawSketchHandlerExternalGeometry.h"

#include "Geometry.h"
#include "TopoShape.h"
#include "Sketcher/SketcherObj.h"
#include "Sketcher/SketcherObjManager.h"
#include "Sketcher/SketchePlane2D.h"
#include "feature/Feature.h"
#include "feature/SketcherFeature.h"
#include "feature/SubShapeRef.h"
#include "core/ViewTool.h"
#include "core/log.h"
#include "editor/Toolbar/sketchToolbar.h"
#include "renderer/SceneView.h"
#include "Interactive/Im3DRenderer.h"
#include "Core/Global/ServiceLocator.h"

#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopAbs.hxx>
#include <BRep_Tool.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Section.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <GCPnts_QuasiUniformDeflection.hxx>
#include <GeomAPI.hxx>
#include <GeomAPI_ProjectPointOnCurve.hxx>
#include <GeomAdaptor_Curve.hxx>
#include <Geom_Circle.hxx>
#include <Geom_Curve.hxx>
#include <Geom_Ellipse.hxx>
#include <Geom_Plane.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <GeomProjLib.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <ElCLib.hxx>
#include <gp_Ax3.hxx>
#include <gp_Elips.hxx>
#include <gp_Pln.hxx>
#include <Precision.hxx>

#include <algorithm>
#include <memory>

namespace MOON {

    namespace
    {
        /** The sketch plane plus the conversions between world and sketch
         * coordinates every projection step needs.
         *
         * Sketch coordinates are (u, v, 0), the space the geometry drawn in the
         * sketch lives in, so both the solver and the viewport can treat a projected
         * curve like any other curve of the sketch. Dropping the off-plane component
         * is exactly an orthographic projection along the plane normal, which is what
         * a sketch references by default. (FreeCAD offers "section" as the
         * alternative, for shapes a projection would smear into one curve.) */
        struct SketchProjection
        {
            SketcherPlane2D plane;
            Base::Vector3d xAxis;
            Base::Vector3d yAxis;
            Base::Vector3d normal;

            explicit SketchProjection(const SketcherPlane2D& p_plane)
                : plane(p_plane)
                , xAxis(p_plane.xAxis)
                , yAxis(p_plane.yAxis)
                , normal(p_plane.normal)
            {
            }

            /** World point -> sketch (u, v, 0), i.e. the point projected onto the
             * plane and expressed in its axes. */
            Base::Vector3d toSketch(const gp_Pnt& p_point) const
            {
                const Base::Vector3d offset(
                    p_point.X() - plane.origin.x,
                    p_point.Y() - plane.origin.y,
                    p_point.Z() - plane.origin.z);
                return Base::Vector3d(offset.Dot(xAxis), offset.Dot(yAxis), 0.0);
            }

            /** The same for a direction: only its in-plane part survives, which is
             * the direction the projection of a curve runs along. */
            Base::Vector2d toSketch(const gp_Vec& p_direction) const
            {
                const Base::Vector3d direction(
                    p_direction.X(), p_direction.Y(), p_direction.Z());
                return Base::Vector2d(direction.Dot(xAxis), direction.Dot(yAxis));
            }
        };

        void addPoint(
            std::vector<std::unique_ptr<Part::Geometry>>& p_result,
            const Base::Vector3d& p_point)
        {
            p_result.push_back(std::make_unique<Part::GeomPoint>(
                Base::Vector3d(p_point.x, p_point.y, 0.0)));
        }

        /** A segment, or the point a segment has collapsed to. */
        void addSegment(
            std::vector<std::unique_ptr<Part::Geometry>>& p_result,
            const Base::Vector3d& p_a,
            const Base::Vector3d& p_b)
        {
            if ((p_b - p_a).Length() < 1e-9) {
                addPoint(p_result, (p_a + p_b) * 0.5);
                return;
            }
            auto geometry = std::make_unique<Part::GeomLineSegment>();
            geometry->setPoints(
                Base::Vector3d(p_a.x, p_a.y, 0.0),
                Base::Vector3d(p_b.x, p_b.y, 0.0));
            p_result.push_back(std::move(geometry));
        }

        /** Turns the image of a curve into a segment when the samples only span a
         * line - what a projection does to a curve that is seen edge on. Going
         * through the samples keeps the extremes of the arc, which a plain chord
         * between its ends would miss.
         * @return false when the samples are not a line. */
        bool tryAddCollapsedSegment(
            std::vector<std::unique_ptr<Part::Geometry>>& p_result,
            const std::vector<Base::Vector3d>& p_samples)
        {
            if (p_samples.empty()) {
                return true;
            }
            if (p_samples.size() == 1) {
                addPoint(p_result, p_samples.front());
                return true;
            }

            // The two samples that are farthest apart span the line.
            size_t first = 0;
            size_t last = 0;
            double longest = -1.0;
            for (size_t i = 0; i < p_samples.size(); ++i) {
                for (size_t j = i + 1; j < p_samples.size(); ++j) {
                    const double distance = (p_samples[i] - p_samples[j]).Sqr();
                    if (distance > longest) {
                        longest = distance;
                        first = i;
                        last = j;
                    }
                }
            }
            const Base::Vector3d span = p_samples[last] - p_samples[first];
            const double length = span.Length();
            if (length < 1e-12) {
                addPoint(p_result, p_samples.front());
                return true;
            }
            Base::Vector3d direction = span;
            direction.Normalize();

            double lowest = 0.0;
            double highest = length;
            double stray = 0.0;
            const Base::Vector3d origin(0.0, 0.0, 0.0);
            for (const Base::Vector3d& sample : p_samples) {
                const Base::Vector3d offset = sample - p_samples[first];
                const double along = offset.Dot(direction);
                lowest = std::min(lowest, along);
                highest = std::max(highest, along);
                stray = std::max(stray, offset.DistanceToLine(origin, direction));
            }
            if (stray > length * 1e-6) {
                return false;
            }
            addSegment(
                p_result,
                p_samples[first] + direction * lowest,
                p_samples[first] + direction * highest);
            return true;
        }

        /** The direction a world direction runs along inside the sketch. Falls back
         * to an axis of the plane when the direction is seen edge on. */
        gp_Dir projectedDirection(
            const gp_Dir& p_direction,
            const SketchProjection& p_projection)
        {
            Base::Vector3d direction(
                p_direction.X(), p_direction.Y(), p_direction.Z());
            Base::Vector2d inPlane = p_projection.toSketch(
                gp_Vec(direction.x, direction.y, direction.z));
            Base::Vector3d result(inPlane.x, inPlane.y, 0.0);
            if (result.Length() < 1e-12) {
                return gp_Dir(
                    p_projection.xAxis.x, p_projection.xAxis.y, p_projection.xAxis.z);
            }
            result.Normalize();
            return gp_Dir(result.x, result.y, 0.0);
        }

        /** Brings both parameters into the same period of a periodic curve, so that
         * "the parameter is between the two" has a meaning. Copied from
         * Geom_TrimmedCurve::setTrim(). */
        void adjustPeriodic(
            const Handle(Geom_Curve)& p_curve,
            double& p_first,
            double& p_last)
        {
            if (!p_curve->IsPeriodic()) {
                return;
            }
            ElCLib::AdjustPeriodic(
                p_curve->FirstParameter(),
                p_curve->LastParameter(),
                std::min(std::abs(p_first - p_last) / 2.0, Precision::PConfusion()),
                p_first,
                p_last);
        }

        /** The range of the projected curve that is the image of the source arc.
         *
         * A projection can swap the ends of a circle or an ellipse (and it can
         * choose the other half of a periodic curve), so the middle point of the
         * source decides which range to keep. FreeCAD's adjustParameterRange(). */
        void adjustParameterRange(
            const BRepAdaptor_Curve& p_source,
            const SketchProjection& p_projection,
            const Handle(Geom_Curve)& p_projected,
            double& p_first,
            double& p_last)
        {
            const auto parameterOf = [&p_projection, &p_projected](const gp_Pnt& p_point)
                {
                    const Base::Vector3d point = p_projection.toSketch(p_point);
                    GeomAPI_ProjectPointOnCurve projection(
                        gp_Pnt(point.x, point.y, 0.0), p_projected);
                    if (projection.NbPoints() < 1) {
                        return p_projected->FirstParameter();
                    }
                    return projection.LowerDistanceParameter();
                };

            const double sourceFirst = p_source.FirstParameter();
            double sourceLast = p_source.LastParameter();
            // An arc can be stored with its end parameter smaller than its start (it
            // then runs through the boundary of the period), so the middle of the arc
            // has to be taken from the range the arc really covers. Circles and
            // ellipses both have a period of 2*pi.
            while (sourceLast < sourceFirst) {
                sourceLast += 2.0 * M_PI;
            }
            const double first = parameterOf(p_source.Value(sourceFirst));
            const double last = parameterOf(p_source.Value(sourceLast));
            double middle = parameterOf(p_source.Value(0.5 * (sourceFirst + sourceLast)));

            p_first = first;
            p_last = last;
            adjustPeriodic(p_projected, p_first, p_last);
            adjustPeriodic(p_projected, p_first, middle);
            // The middle of the source is not between the two ends: the image runs
            // the other way round the curve.
            if (middle > p_last) {
                std::swap(p_first, p_last);
            }
        }

        /** Samples a curve and maps the samples into the sketch. */
        std::vector<Base::Vector3d> sampleImage(
            const TopoDS_Edge& p_edge,
            const BRepAdaptor_Curve& p_curve,
            const SketchProjection& p_projection)
        {
            // The deflection is relative to the size of the edge, so the result
            // follows what the user sees whatever unit the model is in.
            Bnd_Box bounds;
            BRepBndLib::Add(p_edge, bounds);
            double extent = 1.0;
            if (!bounds.IsVoid()) {
                Standard_Real xMin = 0.0, yMin = 0.0, zMin = 0.0;
                Standard_Real xMax = 0.0, yMax = 0.0, zMax = 0.0;
                bounds.Get(xMin, yMin, zMin, xMax, yMax, zMax);
                extent = std::max(
                    std::max(xMax - xMin, yMax - yMin), std::max(zMax - zMin, 1e-9));
            }

            std::vector<Base::Vector3d> samples;
            GCPnts_QuasiUniformDeflection sampler(p_curve, extent * 1e-3);
            if (!sampler.IsDone() || sampler.NbPoints() < 2) {
                return samples;
            }
            samples.reserve(sampler.NbPoints());
            for (int i = 1; i <= sampler.NbPoints(); ++i) {
                samples.push_back(p_projection.toSketch(sampler.Value(i)));
            }
            return samples;
        }

        /** True when the edge covers a whole period, i.e. it is a full circle or a
         * full ellipse and not an arc of the same curve. The parameters of an edge
         * can run through the boundary of the period, hence the wrap. */
        bool isFullPeriod(double p_first, double p_last)
        {
            double range = p_last - p_first;
            while (range < 0.0) {
                range += 2.0 * M_PI;
            }
            return std::abs(range - 2.0 * M_PI) < 1e-9;
        }

        /** Circle seen face on: it stays a circle of the same radius, which is what
         * the solver and the user both want (a spline that looks like a circle
         * cannot be constrained as one). */
        void projectFaceOnCircle(
            const BRepAdaptor_Curve& p_curve,
            const gp_Circ& p_circle,
            const SketchProjection& p_projection,
            std::vector<std::unique_ptr<Part::Geometry>>& p_result)
        {
            const Base::Vector3d center = p_projection.toSketch(p_circle.Location());
            if (isFullPeriod(p_curve.FirstParameter(), p_curve.LastParameter())) {
                auto geometry = std::make_unique<Part::GeomCircle>();
                geometry->setRadius(p_circle.Radius());
                geometry->setCenter(center);
                p_result.push_back(std::move(geometry));
                return;
            }

            Handle(Geom_Circle) curve = new Geom_Circle(gp_Circ(
                gp_Ax2(
                    gp_Pnt(center.x, center.y, 0.0),
                    gp_Dir(0.0, 0.0, 1.0),
                    projectedDirection(p_circle.XAxis().Direction(), p_projection)),
                p_circle.Radius()));
            double first = 0.0;
            double last = 0.0;
            adjustParameterRange(p_curve, p_projection, curve, first, last);

            auto geometry = std::make_unique<Part::GeomArcOfCircle>();
            geometry->setHandle(
                new Geom_TrimmedCurve(curve, first, last));
            p_result.push_back(std::move(geometry));
        }

        /** The projection of a circle that is tilted against the sketch plane: an
         * ellipse whose major axis is the diameter that stays parallel to the plane
         * and whose minor radius is the radius seen at an angle. */
        void projectTiltedCircle(
            const TopoDS_Edge& p_edge,
            const BRepAdaptor_Curve& p_curve,
            const gp_Circ& p_circle,
            const SketchProjection& p_projection,
            std::vector<std::unique_ptr<Part::Geometry>>& p_result)
        {
            const gp_Dir axis = p_circle.Axis().Direction();
            const double cosAngle = p_projection.normal.Dot(
                Base::Vector3d(axis.X(), axis.Y(), axis.Z()));

            // Seen edge on: with no minor radius left there is no ellipse, only the
            // segment the circle collapses to.
            const double majorRadius = p_circle.Radius();
            const double minorRadius = majorRadius * std::abs(cosAngle);
            if (minorRadius < majorRadius * 1e-9) {
                tryAddCollapsedSegment(
                    p_result, sampleImage(p_edge, p_curve, p_projection));
                return;
            }

            const Base::Vector3d center = p_projection.toSketch(p_circle.Location());
            // The major axis is the in-plane direction perpendicular to the axis of
            // the circle: that is the diameter the projection does not shorten.
            Base::Vector3d majorAxis = p_projection.normal.Cross(
                Base::Vector3d(axis.X(), axis.Y(), axis.Z()));
            if (majorAxis.Length() < 1e-12) {
                majorAxis = p_projection.xAxis;
            }
            majorAxis.Normalize();

            Handle(Geom_Ellipse) curve = new Geom_Ellipse(gp_Elips(
                gp_Ax2(
                    gp_Pnt(center.x, center.y, 0.0),
                    gp_Dir(0.0, 0.0, 1.0),
                    gp_Dir(majorAxis.x, majorAxis.y, 0.0)),
                majorRadius,
                minorRadius));
            // (IsClosed() of the adaptor would answer for the underlying circle, which
            // is periodic whether or not the edge covers the whole of it.)
            if (isFullPeriod(p_curve.FirstParameter(), p_curve.LastParameter())) {
                auto geometry = std::make_unique<Part::GeomEllipse>();
                geometry->setHandle(curve);
                p_result.push_back(std::move(geometry));
                return;
            }

            double first = 0.0;
            double last = 0.0;
            adjustParameterRange(p_curve, p_projection, curve, first, last);
            auto geometry = std::make_unique<Part::GeomArcOfEllipse>();
            geometry->setHandle(new Geom_TrimmedCurve(curve, first, last));
            p_result.push_back(std::move(geometry));
        }

        /** Anything the analytic cases above do not cover (splines, and whatever a
         * model carries in a face that is not a plane). */
        void projectCurveFallback(
            const TopoDS_Edge& p_edge,
            const BRepAdaptor_Curve& p_curve,
            const SketchProjection& p_projection,
            std::vector<std::unique_ptr<Part::Geometry>>& p_result)
        {
            std::vector<Base::Vector3d> samples
                = sampleImage(p_edge, p_curve, p_projection);
            if (samples.size() < 2) {
                if (!samples.empty()) {
                    addPoint(p_result, samples.front());
                }
                return;
            }
            if (tryAddCollapsedSegment(p_result, samples)) {
                return;
            }

            const bool closed
                = (samples.front() - samples.back()).Length()
                < 1e-6 * (samples.back() - samples.front()).Length()
                + 1e-12;
            if (closed) {
                // The samples of a closed curve start and end on the same point,
                // which the periodic interpolation must not see twice.
                samples.pop_back();
            }
            std::vector<gp_Pnt> points;
            points.reserve(samples.size());
            for (const Base::Vector3d& sample : samples) {
                points.emplace_back(sample.x, sample.y, 0.0);
            }
            auto geometry = std::make_unique<Part::GeomBSplineCurve>();
            geometry->interpolate(points, closed);
            p_result.push_back(std::move(geometry));
        }

        /** Projects one edge of the source shape.
         *
         * The type of the source curve decides the type of the result - the same
         * idea as FreeCAD's processEdge(). BRepAdaptor_Curve is used on purpose:
         * it applies the location of the edge, and an imported model regularly
         * carries its arcs in a local frame. Taking the raw curve there would
         * project geometry that is somewhere else (and, for a circle, tilted
         * against the sketch plane instead of parallel to it). */
        void projectEdgeShape(
            const TopoDS_Edge& p_edge,
            const SketchProjection& p_projection,
            std::vector<std::unique_ptr<Part::Geometry>>& p_result)
        {
            const BRepAdaptor_Curve curve(p_edge);
            if (curve.GetType() == GeomAbs_Line) {
                addSegment(
                    p_result,
                    p_projection.toSketch(curve.Value(curve.FirstParameter())),
                    p_projection.toSketch(curve.Value(curve.LastParameter())));
                return;
            }
            if (curve.GetType() == GeomAbs_Circle) {
                const gp_Circ circle = curve.Circle();
                const gp_Dir axis = circle.Axis().Direction();
                const double cosAngle = p_projection.normal.Dot(
                    Base::Vector3d(axis.X(), axis.Y(), axis.Z()));
                if (std::abs(cosAngle) > 1.0 - 1e-9) {
                    projectFaceOnCircle(curve, circle, p_projection, p_result);
                }
                else {
                    projectTiltedCircle(p_edge, curve, circle, p_projection, p_result);
                }
                return;
            }
            if (curve.GetType() == GeomAbs_Ellipse) {
                const gp_Elips ellipse = curve.Ellipse();
                const gp_Dir axis = ellipse.Axis().Direction();
                const double cosAngle = p_projection.normal.Dot(
                    Base::Vector3d(axis.X(), axis.Y(), axis.Z()));
                if (std::abs(cosAngle) > 1.0 - 1e-9) {
                    // Face on, the ellipse only moves along the normal and keeps its
                    // size; the axes are taken as they project.
                    const Base::Vector3d center
                        = p_projection.toSketch(ellipse.Location());
                    Handle(Geom_Ellipse) curveHandle = new Geom_Ellipse(gp_Elips(
                        gp_Ax2(
                            gp_Pnt(center.x, center.y, 0.0),
                            gp_Dir(0.0, 0.0, 1.0),
                            projectedDirection(ellipse.XAxis().Direction(), p_projection)),
                        ellipse.MajorRadius(),
                        ellipse.MinorRadius()));
                    if (isFullPeriod(curve.FirstParameter(), curve.LastParameter())) {
                        auto geometry = std::make_unique<Part::GeomEllipse>();
                        geometry->setHandle(curveHandle);
                        p_result.push_back(std::move(geometry));
                        return;
                    }
                    double first = 0.0;
                    double last = 0.0;
                    adjustParameterRange(curve, p_projection, curveHandle, first, last);
                    auto geometry = std::make_unique<Part::GeomArcOfEllipse>();
                    geometry->setHandle(new Geom_TrimmedCurve(curveHandle, first, last));
                    p_result.push_back(std::move(geometry));
                    return;
                }
            }
            projectCurveFallback(p_edge, curve, p_projection, p_result);
        }

        const char* curveTypeName(GeomAbs_CurveType p_type)
        {
            switch (p_type) {
            case GeomAbs_Line: return "line";
            case GeomAbs_Circle: return "circle";
            case GeomAbs_Ellipse: return "ellipse";
            case GeomAbs_Hyperbola: return "hyperbola";
            case GeomAbs_Parabola: return "parabola";
            case GeomAbs_BezierCurve: return "bezier";
            case GeomAbs_BSplineCurve: return "spline";
            case GeomAbs_OffsetCurve: return "offset curve";
            default: return "other curve";
            }
        }

        const char* geometryTypeName(const Part::Geometry* p_geometry)
        {
            if (p_geometry == nullptr) {
                return "nothing";
            }
            if (p_geometry->is<Part::GeomPoint>()) {
                return "point";
            }
            if (p_geometry->is<Part::GeomLineSegment>()) {
                return "line";
            }
            if (p_geometry->is<Part::GeomCircle>()) {
                return "circle";
            }
            if (p_geometry->is<Part::GeomArcOfCircle>()) {
                return "arc";
            }
            if (p_geometry->is<Part::GeomEllipse>()) {
                return "ellipse";
            }
            if (p_geometry->is<Part::GeomArcOfEllipse>()) {
                return "arc of ellipse";
            }
            if (p_geometry->is<Part::GeomBSplineCurve>()) {
                return "spline";
            }
            return "curve";
        }

        /** Projects an edge and reports what became of it: a projection that
         * collapses an arc onto a line is decided by the geometry, and the log is
         * what tells the two cases apart when a reference does not look like the
         * shape it was taken from. */
        void projectEdge(
            const TopoDS_Edge& p_edge,
            const SketchProjection& p_projection,
            std::vector<std::unique_ptr<Part::Geometry>>& p_result)
        {
            const size_t before = p_result.size();
            const BRepAdaptor_Curve curve(p_edge);
            projectEdgeShape(p_edge, p_projection, p_result);

            std::string produced;
            for (size_t i = before; i < p_result.size(); ++i) {
                produced += produced.empty() ? "" : " + ";
                produced += geometryTypeName(p_result[i].get());
            }
            CORE_INFO(
                "[ExternalGeo] projected a {0} edge to {1}",
                curveTypeName(curve.GetType()),
                produced.empty() ? "<nothing>" : produced);
        }

        /** Fits the pieces of one circle back together.
         *
         * A plane cutting a sphere (or any revolve) comes back as several arcs of the
         * same circle, and as far as the sketch is concerned they are one curve - it
         * would only have to constrain them against each other. FreeCAD does the same
         * in its fitArcs().
         *
         * @return the single circle or arc, or null when the pieces are not one
         *         circle after all. */
        std::unique_ptr<Part::Geometry> mergeCirclePieces(
            const std::vector<const Part::Geometry*>& p_pieces)
        {
            double radius = 0.0;
            Base::Vector3d center;
            std::vector<std::pair<Base::Vector3d, Base::Vector3d>> ends;
            for (const Part::Geometry* piece : p_pieces) {
                if (piece == nullptr || !piece->is<Part::GeomArcOfCircle>()) {
                    return nullptr;
                }
                const auto* arc = static_cast<const Part::GeomArcOfCircle*>(piece);
                if (radius == 0.0) {
                    radius = arc->getRadius();
                    center = arc->getCenter();
                }
                else if (std::abs(radius - arc->getRadius())
                    > std::max(radius, 1.0) * 1e-6) {
                    return nullptr;  // not the same circle: leave the pieces alone
                }
                ends.emplace_back(arc->getStartPoint(), arc->getEndPoint());
            }
            if (radius == 0.0 || ends.empty()) {
                return nullptr;
            }

            // The ends of the union are the endpoints that only one piece owns; the
            // junctions between pieces are shared by two and cancel out.
            const auto samePoint = [&radius](const Base::Vector3d& p_a, const Base::Vector3d& p_b)
                {
                    return (p_a - p_b).Length() < std::max(radius, 1.0) * 1e-9;
                };
            std::vector<Base::Vector3d> loose;
            for (const auto& [start, end] : ends) {
                for (const Base::Vector3d& point : { start, end }) {
                    const auto found = std::find_if(
                        loose.begin(), loose.end(),
                        [&](const Base::Vector3d& existing) { return samePoint(existing, point); });
                    if (found == loose.end()) {
                        loose.push_back(point);
                    }
                    else {
                        loose.erase(found);
                    }
                }
            }

            if (loose.size() != 2) {
                // Nothing is loose: the pieces close the circle.
                auto geometry = std::make_unique<Part::GeomCircle>();
                geometry->setRadius(radius);
                geometry->setCenter(center);
                return geometry;
            }

            // A point in the middle of the first piece tells the fitting which way
            // round the arc runs. It has to be a point *on* the curve - the middle of
            // the chord is not one - so the arc is evaluated at its middle parameter.
            const auto* firstPiece
                = static_cast<const Part::GeomArcOfCircle*>(p_pieces.front());
            double rangeFirst = 0.0;
            double rangeLast = 0.0;
            firstPiece->getRange(rangeFirst, rangeLast, false);
            Handle(Geom_Curve) firstCurve
                = Handle(Geom_Curve)::DownCast(firstPiece->handle());
            if (firstCurve.IsNull()) {
                return nullptr;
            }
            const gp_Pnt through = firstCurve->Value(0.5 * (rangeFirst + rangeLast));
            const gp_Pnt first(loose[0].x, loose[0].y, 0.0);
            const gp_Pnt last(loose[1].x, loose[1].y, 0.0);
            GC_MakeArcOfCircle fitting(first, through, last);
            if (!fitting.IsDone()) {
                return nullptr;
            }
            auto geometry = std::make_unique<Part::GeomArcOfCircle>();
            geometry->setHandle(fitting.Value());
            return geometry;
        }

        /** The section of the referenced shape with the sketch plane: the curve where
         * the shape is actually cut, not the shadow it casts (see projectEdge for the
         * latter). It lies in the plane by construction, so it needs no projection -
         * what a sketch drawn on a section wants to constrain to is the cut itself.
         * FreeCAD calls this the intersection mode of its external geometry. */
        void sectionIntoSketch(
            const TopoDS_Shape& p_shape,
            const SketchProjection& p_projection,
            std::vector<std::unique_ptr<Part::Geometry>>& p_result)
        {
            const gp_Pln plane(
                gp_Pnt(
                    p_projection.plane.origin.x,
                    p_projection.plane.origin.y,
                    p_projection.plane.origin.z),
                gp_Dir(
                    p_projection.normal.x,
                    p_projection.normal.y,
                    p_projection.normal.z));

            BRepAlgoAPI_Section section(p_shape, plane, Standard_False);
            // A section of a curved face is rarely exact; the approximation is what
            // makes it come out at all (FreeCAD sets it as well).
            section.Approximation(Standard_True);
            section.Build();
            if (!section.IsDone() || section.Shape().IsNull()) {
                CORE_WARN("[ExternalGeo] the section with the sketch plane failed");
                return;
            }

            const size_t firstSectionGeo = p_result.size();
            for (TopExp_Explorer explorer(section.Shape(), TopAbs_EDGE);
                explorer.More();
                explorer.Next()) {
                projectEdge(TopoDS::Edge(explorer.Current()), p_projection, p_result);
            }

            // The pieces of one circle are one curve for the sketch.
            std::vector<const Part::Geometry*> pieces;
            for (size_t i = firstSectionGeo; i < p_result.size(); ++i) {
                pieces.push_back(p_result[i].get());
            }
            bool merged = false;
            if (pieces.size() > 1) {
                if (std::unique_ptr<Part::Geometry> fitted = mergeCirclePieces(pieces)) {
                    p_result.resize(firstSectionGeo);
                    p_result.push_back(std::move(fitted));
                    merged = true;
                }
            }

            // The corners of the section are useful constraint targets on their own,
            // so they are imported as points (FreeCAD does the same).
            //
            // After a merge they are skipped: those vertices are the seams where the
            // pieces used to meet, not features of the cut.
            if (merged) {
                return;
            }
            for (TopExp_Explorer explorer(section.Shape(), TopAbs_VERTEX);
                explorer.More();
                explorer.Next()) {
                const gp_Pnt point = BRep_Tool::Pnt(TopoDS::Vertex(explorer.Current()));
                addPoint(p_result, p_projection.toSketch(point));
            }
            if (p_result.size() == firstSectionGeo) {
                CORE_INFO(
                    "[ExternalGeo] the section is empty: the referenced shape does not "
                    "cross the sketch plane (try the projection flavour instead)");
            }
        }
    }

    /** Projects one sub-shape of another feature into the sketch plane. */
    static std::vector<std::unique_ptr<Part::Geometry>> projectIntoSketch(
        const TopoDS_Shape& p_shape,
        const SketcherPlane2D& p_plane,
        bool p_intersection = false)
    {
        const SketchProjection projection(p_plane);
        std::vector<std::unique_ptr<Part::Geometry>> result;
        if (p_shape.IsNull()) {
            return result;
        }

        // The section is taken against the plane itself, whatever the shape is: a face
        // cuts along its outline, a solid along its whole cross section.
        if (p_intersection) {
            sectionIntoSketch(p_shape, projection, result);
            return result;
        }

        // A vertex becomes a point.
        if (p_shape.ShapeType() == TopAbs_VERTEX) {
            addPoint(
                result,
                projection.toSketch(BRep_Tool::Pnt(TopoDS::Vertex(p_shape))));
            return result;
        }

        // A face is referenced through its boundary.
        std::vector<TopoDS_Edge> edges;
        const bool fromFace = p_shape.ShapeType() == TopAbs_FACE;
        if (fromFace) {
            for (TopExp_Explorer explorer(p_shape, TopAbs_EDGE); explorer.More(); explorer.Next()) {
                edges.push_back(TopoDS::Edge(explorer.Current()));
            }
        }
        else if (p_shape.ShapeType() == TopAbs_EDGE) {
            edges.push_back(TopoDS::Edge(p_shape));
        }

        for (const TopoDS_Edge& edge : edges) {
            projectEdge(edge, projection, result);
        }

        // A planar face seen edge on collapses onto a single line, and its boundary
        // would hand back several segments that lie on top of each other. FreeCAD
        // reduces them to one segment as well, so the sketch gets one reference to
        // constrain to instead of a pile.
        if (fromFace && result.size() > 1) {
            gp_Pln facePlane;
            const Part::TopoShape faceShape(p_shape);
            const bool planar = faceShape.findPlane(facePlane);
            const gp_Dir normal(
                projection.normal.x, projection.normal.y, projection.normal.z);
            if (planar
                && facePlane.Axis().Direction().IsNormal(normal, Precision::Angular())) {
                std::vector<Base::Vector3d> endpoints;
                for (const std::unique_ptr<Part::Geometry>& geo : result) {
                    if (geo == nullptr || !geo->is<Part::GeomLineSegment>()) {
                        endpoints.clear();
                        break;  // not the flat picture this reduction is for
                    }
                    const auto* line = static_cast<const Part::GeomLineSegment*>(geo.get());
                    endpoints.push_back(line->getStartPoint());
                    endpoints.push_back(line->getEndPoint());
                }
                if (endpoints.size() >= 2) {
                    std::vector<std::unique_ptr<Part::Geometry>> reduced;
                    tryAddCollapsedSegment(reduced, endpoints);
                    if (reduced.size() == 1) {
                        CORE_INFO(
                            "[ExternalGeo] a planar face seen edge on was reduced from "
                            "{0} segments to one line",
                            result.size());
                        return reduced;
                    }
                }
            }
        }
        return result;
    }

    /** The name of a topology leaf of another feature: "<Type>_<index>", the form
     * the sub-shape references are written in (see ResolveSubShapeRef). The
     * picking pass hands back the actor, not the element, so a pick that landed on
     * a group actor (Solid_*, Shell_*) or on a whole body names something the
     * resolver cannot read - it has to be rejected here, with a message, instead
     * of turning into a reference that quietly resolves to nothing. */
    static bool isSubShapeReference(const std::string& p_reference)
    {
        return p_reference.rfind("Face_", 0) == 0
            || p_reference.rfind("Edge_", 0) == 0;
    }

    DrawSketchHandlerExternalGeometry::DrawSketchHandlerExternalGeometry(const std::string& name)
        : DrawSketchHandler(name)
    {
    }

    DrawSketchHandlerExternalGeometry::~DrawSketchHandlerExternalGeometry()
    {
    }

    void DrawSketchHandlerExternalGeometry::onUpdate()
    {
        // The tool draws nothing of its own: what the cursor is on is shown by the
        // highlight the picking pass puts on the sub-shape under it.
    }

    void DrawSketchHandlerExternalGeometry::onMouseMove()
    {
        // The sketch must not pick its own geometry while the tool runs; the sketch
        // sees that a handler is active (SketcherObj::onUpdate) and stays out of the
        // way, so there is nothing to do here.
    }

    void DrawSketchHandlerExternalGeometry::onKeyPress(const std::string& key)
    {
        if (key == "ESCAPE") {
            quit();
        }
    }

    void DrawSketchHandlerExternalGeometry::quit()
    {
        setActive(false);
        // Leave the toolbar buttons in step with the widget: the mode lives here,
        // but it is the buttons the user sees.
        GetService(SketchToolbar).uncheckExternalGeometry();
    }

    void DrawSketchHandlerExternalGeometry::onLeftMousePressed()
    {
        if (!isActived()) {
            return;
        }
        pickAndAddReference();
    }

    bool DrawSketchHandlerExternalGeometry::pickAndAddReference()
    {
        SketcherObj* sketch
            = SketcherObjManager::instance().GetCurrentActiveSketcherObj();
        if (sketch == nullptr) {
            CORE_WARN("[ExternalGeo] no sketch is being edited");
            return false;
        }

        // The pick pass already resolved the actor under the cursor (the scene view
        // keeps the last hover pick), so what is highlighted is what is referenced.
        const auto pickingResult = m_sceneView->GetPickResult();
        if (!pickingResult.has_value()) {
            return false;
        }
        const auto picked
            = std::get_if<Tools::Utils::OptRef<::Core::ECS::Actor>>(&pickingResult.value());
        if (picked == nullptr || !picked->has_value()) {
            return false;
        }
        ::Core::ECS::Actor* actor = &picked->value();

        Feature* source = nullptr;
        std::string reference;
        if (!ViewTool::getActorBasedFeature(actor, source, reference)) {
            CORE_WARN(
                "[ExternalGeo] '{0}' does not belong to a feature, so it cannot be "
                "referenced",
                actor->GetName());
            return false;
        }
        if (!isSubShapeReference(reference)) {
            CORE_WARN(
                "[ExternalGeo] '{0}.{1}' is not a face or an edge of the feature: pick "
                "the face or the edge itself",
                source->GetName(),
                reference);
            return false;
        }
        // The sketch's own geometry can never be a reference of itself.
        if (source == SketcherObjManager::instance().GetCurrentActiveSketcherFeature()) {
            CORE_WARN(
                "[ExternalGeo] '{0}' belongs to the sketch itself",
                reference);
            return false;
        }

        // Resolved against the source as it is now. The names are not kept: what the
        // sketch stores is the projected curve, not the reference (see the note on
        // SketcherObj::addExternalGeometry).
        std::vector<std::string> names;
        Part::TopoShape subShape = ResolveSubShapeRef(*source, reference, names);
        if (subShape.isNull()) {
            CORE_WARN(
                "[ExternalGeo] '{0}' of {1} cannot be resolved",
                reference,
                source->GetName());
            return false;
        }

        const bool section = m_mode == EMode::Section;
        std::vector<std::unique_ptr<Part::Geometry>> curves
            = projectIntoSketch(subShape.getShape(), sketch->getPlane(), section);
        if (curves.empty()) {
            CORE_WARN(
                "[ExternalGeo] '{0}' of {1} {2} to nothing",
                reference,
                source->GetName(),
                section ? "does not cross the sketch plane, so its section comes out empty"
                        : "projects to nothing");
            return false;
        }

        const size_t count = curves.size();
        const int geoId = sketch->addExternalGeometry(std::move(curves));
        if (geoId != SketcherObj::NoGeoId) {
            CORE_INFO(
                "[ExternalGeo] {0}: {1} '{2}' of {3} -> {4} curve(s)",
                sketch->getName(),
                section ? "sectioned" : "projected",
                reference,
                source->GetName(),
                count);
            return true;
        }
        CORE_WARN("[ExternalGeo] {0}: nothing was added", sketch->getName());
        return false;
    }
}
