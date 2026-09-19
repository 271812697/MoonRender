#include "Sketcher/SketcherObj.h"
#include "editor/Toolbar/sketchToolbar.h"
#include "Geometry.h"
#include "renderer/SceneView.h"

#include "Core/Global/ServiceLocator.h"
#include "base/Tools.h"
#include "core/log.h"
#include "core/TopoNameDebug.h"
#include "core/ViewTool.h"
#include "core/component/CTopoShape.h"
#include "feature/Feature.h"
#include "feature/SubShapeRef.h"

#include "ElementMap.h"
#include "MappedElement.h"
#include "TopoShapeOpCode.h"

#include <TopoDS.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <ShapeFix_Wire.hxx>
#include <BRep_Builder.hxx>
#include <GeomAPI.hxx>
#include <Geom2dAPI_InterCurveCurve.hxx>
#include <Geom2dAPI_ProjectPointOnCurve.hxx>
#include <BRep_Tool.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Section.hxx>
#include <ElCLib.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <Precision.hxx>
#include <GCPnts_QuasiUniformDeflection.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <GeomAPI_ProjectPointOnCurve.hxx>
#include <GeomAdaptor_Curve.hxx>
#include <Geom_Ellipse.hxx>
#include <GeomProjLib.hxx>
#include <Geom_Circle.hxx>
#include <Geom_Curve.hxx>
#include <Geom_Plane.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <gp_Ax3.hxx>
#include <gp_Elips.hxx>
#include <gp_Pln.hxx>
#include <algorithm>
#include <memory>
namespace MOON {

    static bool areParamsWithinApproximation(double param1, double param2)
    {
        // From testing: 500x (or 0.000050) is needed in order to not falsely distinguish points
        // calculated with seekTrimPoints
        return (std::abs(param1 - param2) < Precision::PApproximation());
    }
    static bool arePointsWithinPrecision(const Base::Vector3d& point1, const Base::Vector3d& point2)
    {
        // From testing: 500x (or 0.000050) is needed in order to not falsely distinguish points
        // calculated with seekTrimPoints
        return ((point1 - point2).Length() < 500 * Precision::Confusion());
    }

    /** Name of the edge built from geometry p_geoId of the sketch, and of its end
     * points.
     *
     * Same convention FreeCAD's SketchObject::convertSubName() uses ("g<id>" for
     * the edge, "g<id>v<pos>" for a vertex), and deliberately built from the
     * sketch geometry id instead of the index inside the finished shape: the
     * geometries of a sketch keep their id while it is edited, the enumeration
     * order of the resulting shape does not. */
    static std::string sketchElementName(int p_geoId, int p_pointPos = -1)
    {
        std::string name = "g" + std::to_string(p_geoId);
        if (p_pointPos >= 0) {
            name += "v" + std::to_string(p_pointPos);
        }
        return name;
    }

    /** Gives one geometry's edge (and its end points) the names downstream
     * operations will extend. */
    static void nameSketchEdge(Part::TopoShape& p_shape, int p_geoId, const Part::Geometry* p_geo)
    {
        if (!p_shape.hasElementMap()) {
            p_shape.resetElementMap(std::make_shared<Data::ElementMap>());
        }
        p_shape.setElementName(
            Data::IndexedName::fromConst("Edge", 1),
            Data::MappedName::fromRawData(sketchElementName(p_geoId).c_str()),
            0L);

        if (p_geo == nullptr || !p_geo->isDerivedFrom<Part::GeomBoundedCurve>()) {
            return;
        }
        // The end points are named as well, so "the start of geometry 3" can be
        // traced through the chain too.
        const auto* curve = static_cast<const Part::GeomBoundedCurve*>(p_geo);
        const Base::Vector3d start = curve->getStartPoint();
        const Base::Vector3d end = curve->getEndPoint();

        TopTools_IndexedMapOfShape vertexMap;
        TopExp::MapShapes(p_shape.getShape(), TopAbs_VERTEX, vertexMap);
        for (int index = 1; index <= vertexMap.Extent(); ++index) {
            const gp_Pnt gp = BRep_Tool::Pnt(TopoDS::Vertex(vertexMap(index)));
            const Base::Vector3d point(gp.X(), gp.Y(), gp.Z());
            int pointPos = -1;
            if ((point - start).Length() < Precision::Confusion()) {
                pointPos = static_cast<int>(Sketcher::PointPos::start);
            }
            else if ((point - end).Length() < Precision::Confusion()) {
                pointPos = static_cast<int>(Sketcher::PointPos::end);
            }
            if (pointPos < 0) {
                continue;
            }
            p_shape.setElementName(
                Data::IndexedName::fromConst("Vertex", index),
                Data::MappedName::fromRawData(sketchElementName(p_geoId, pointPos).c_str()),
                0L);
        }
    }

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
 
	SketcherObj::SketcherObj() :EventWidget("SketcherObj")
    {
        setActive(true);
    }
    SketcherObj::~SketcherObj()
    {
        for (Sketcher::Constraint* c : mConstraintList) {
            delete c;
        }
        mConstraintList.clear();
    }
    bool SketcherObj::InEdit() const
    {
        return isInEdit;
    }
    void SketcherObj::makeDone()
    {
        isInEdit = false;
        auto& view = GetService(Editor::Panels::SceneView);
        view.GetCameraController().EnableRotate(true);
        doneWireShape = toShape();
        if (!doneWireShape.isEmpty()) {
            doneFaceShape = doneWireShape.makeElementFace(nullptr, "Part::FaceMakerBullseye");
        }
        GetService(SketchToolbar).disableAllHandlers();
    }
    int SketcherObj::solve(bool updateGeoAfterSolving)
    {
        //Reset
        solvedSketch.resetInitMove();
        //Set Up geometry and contraint
        std::vector<Part::Geometry*> GeoList;
        for (int i = 0; i < mGeoList.size(); i++) {
            GeoList.push_back(mGeoList[i].get());
        }
        // External geometry goes last: setUpSketch() takes the trailing extGeoCount
        // entries as blocked, i.e. as parameters the sketch may use but never move.
        // The projections are refreshed first, so the solver sees the sources as they
        // are now.
        {
            // A refresh can change how many curves a reference has (its source may have
            // been rebuilt), and the ids the constraints hold count from the end of the
            // block - so the references are rebuilt together with the block.
            const std::vector<std::pair<int, int>> keysBefore = externalCurveKeys();
            updateExternalGeometry();
            if (getExternalCurveCount() != static_cast<int>(keysBefore.size())) {
                remapExternalReferences(keysBefore);
            }
        }
        int externalCount = 0;
        for (const ExternalGeometry& external : mExternalGeometry) {
            for (const std::unique_ptr<Part::Geometry>& geo : external.geos) {
                GeoList.push_back(geo.get());
                ++externalCount;
            }
        }
        lastDoF=solvedSketch.setUpSketch(
            GeoList, mConstraintList, externalCount);
        //restrive the solver information
        retrieveSolverDiagnostics();

        lastSolverStatus = GCS::Failed;
        int err = 0;
        if (lastHasRedundancies) {// redundant constraints
            err = -2;
        }
        if (lastDoF < 0) {// over-constrained sketch
            err = -4;
        }
        else if (lastHasConflict) {// conflicting constraints
            // The situation is exactly the same as in the over-constrained situation.
            err = -3;
        }
        else if (lastHasMalformedConstraints) {
            err = -5;
        }
        else {
            lastSolverStatus = solvedSketch.solve();
            if (lastSolverStatus != 0) {// solving
                err = -1;
            }
        }
        if (err==0) {
            // Replace the geometry in place. FreeCAD keeps the geometry
            // property list untouched when there is no change; here we rebuild
            // the internal list directly and never route through
            // deleteGeometries() (that would wipe constraints referencing the
            // very elements we just solved).
            for (auto& geo : mGeoList) {
                mGeoSegment.erase(geo.get());
            }
            mGeoList.clear();
            std::vector<Part::Geometry*> geomlist = solvedSketch.extractGeometry();
            for (Part::Geometry* geo : geomlist) {
                addGeometry(geo);  // copies into owned storage
            }
            for (Part::Geometry* geo : geomlist) {
                delete geo;        // extractGeometry() hands out clones
            }
        }
        return err;
    }
 
    int SketcherObj::fillet(int GeoId1, int GeoId2, const Base::Vector3d& refPnt1, const Base::Vector3d& refPnt2, double radius, bool trim, bool createCorner, bool chamfer)
    {
        if (GeoId1 < 0 || GeoId1 > getHighestCurveIndex() || GeoId2 < 0 || GeoId2 > getHighestCurveIndex()) {
            return -1;
        }
        // If either of the two input lines are locked, don't try to trim since it won't work anyway
        Part::Geometry* geo1 = getGeometry(GeoId1);
        Part::Geometry* geo2 = getGeometry(GeoId2);
        int pos1 = 0;
        int pos2 = 0;
        bool reverse = false;
        std::unique_ptr<Part::GeomArcOfCircle> arc(createFilletGeometry(geo1, geo2, refPnt1, refPnt2, radius, pos1, pos2, reverse));
        if (!arc) {
            return -1;
        }

        int filletId = addGeometry(arc.get());
        if (filletId < 0) {
            return -1;
        }

        int PosId1 = static_cast<int>(pos1);
        int PosId2 = static_cast<int>(pos2);
        int filletPosId1 = -1;
        int filletPosId2 = -1;

        Base::Vector3d p1 = arc->getStartPoint(true);
        Base::Vector3d p2 = arc->getEndPoint(true);

        if (trim) {
            //if (reverse) {
            //    moveGeometry(GeoId1, PosId1, p1, false, true);
            //    moveGeometry(GeoId2, PosId2, p2, false, true);
            //}
            //else {
            //    moveGeometry(GeoId1, PosId1, p2, false, true);
            //    moveGeometry(GeoId2, PosId2, p1, false, true);
            //}
            auto* line1 = static_cast<Part::GeomLineSegment*>(geo1);
            auto* line2 = static_cast<Part::GeomLineSegment*>(geo2);

            auto s1= line1->getStartPoint();
            auto e1 = line1->getEndPoint();
            auto s2 = line2->getStartPoint();
            auto e2 = line2->getEndPoint();
           if (reverse) {
               if (PosId1 == 1) {//>0
                   line1->setPoints(p1,e1);
               }
               else if(PosId1==2)
               {
                   line1->setPoints(s1, p1);
               }
               if (PosId2 == 1) {//>0
                   line2->setPoints(p2, e2);
               }
               else if (PosId2 == 2)
               {
                   line2->setPoints(s2, p2);
               }
            }
            else {
               if (PosId1 == 1) {//>0
                   line1->setPoints(p2, e1);
               }
               else if (PosId1 == 2)
               {
                   line1->setPoints(s1, p2);
               }
               if (PosId2 == 1) {//>0
                   line2->setPoints(p1, e2);
               }
               else if (PosId2 == 2)
               {
                   line2->setPoints(s2, p1);
               }
            }
           updateGeoSegment(GeoId1);
           updateGeoSegment(GeoId2);
        }

        if (chamfer) {
            auto line = std::make_unique<Part::GeomLineSegment>();
            line->setPoints(p1, p2);
            int lineGeoId = addGeometry(line.get());
        }
        return 0;
    }
    bool SketcherObj::seekTrimPoints(int geometryIndex,
        const Base::Vector3d& point,
        int& geometryIndex1,
        Base::Vector3d& intersect1,
        int& geometryIndex2,
        Base::Vector3d& intersect2, double& u1, double& u2)
    {
        if (geometryIndex < 0
            || static_cast<size_t>(geometryIndex) >= mGeoList.size()
            || mGeoList[geometryIndex] == nullptr) {
            return false;
        }
        gp_Pln plane(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1));

        Standard_Boolean periodic = Standard_False;
        double period = 0;
        Handle(Geom2d_Curve) primaryCurve;
        Handle(Geom_Geometry) geom = (mGeoList[geometryIndex])->handle();
        Handle(Geom_Curve) curve3d = Handle(Geom_Curve)::DownCast(geom);

        if (curve3d.IsNull()) {
            return false;
        }
        else {
            primaryCurve = GeomAPI::To2d(curve3d, plane);
            // To2d() hands back a null handle for a curve that cannot be written
            // in that plane; everything below would walk into it.
            if (primaryCurve.IsNull()) {
                return false;
            }
            periodic = primaryCurve->IsPeriodic();
            if (periodic) {
                period = primaryCurve->Period();
            }
        }

        // create the intersector and projector functions
        Geom2dAPI_InterCurveCurve Intersector;
        Geom2dAPI_ProjectPointOnCurve Projector;

        // find the parameter of the picked point on the primary curve
        Projector.Init(gp_Pnt2d(point.x, point.y), primaryCurve);
        // A projection that found nothing - a degenerate curve, a curve whose
        // parameter range is empty, a pick that is not a number - has no parameter
        // to give back. Asking for one anyway reads into an extrema that was never
        // computed, which is what crashed the trim tool while the mouse moved.
        if (Projector.NbPoints() < 1) {
            return false;
        }
        double pickedParam = Projector.LowerDistanceParameter();

        // find intersection points
        geometryIndex1 = -1;
        geometryIndex2 = -1;
        double param1 = -1e10, param2 = 1e10;
        gp_Pnt2d p1, p2;
        Handle(Geom2d_Curve) secondaryCurve;
        for (int id = 0; id < int(mGeoList.size()); id++) {
            // #0000624: Trim tool doesn't work with construction lines
            if (id != geometryIndex /* && !geomlist[id]->Construction*/) {
                geom = (mGeoList[id])->handle();
                curve3d = Handle(Geom_Curve)::DownCast(geom);
                if (!curve3d.IsNull()) {
                    secondaryCurve = GeomAPI::To2d(curve3d, plane);
                    // perform the curves intersection

                    std::vector<gp_Pnt2d> points;

                    // #2463 Check for endpoints of secondarycurve on primary curve
                    // If the OCCT Intersector should detect endpoint tangency when trimming, then
                    // this is just a work-around until that bug is fixed.
                    // https://www.freecad.org/tracker/view.php?id=2463
                    // https://tracker.dev.opencascade.org/view.php?id=30217
                    if (mGeoList[id]->isDerivedFrom<Part::GeomBoundedCurve>()) {

                        Part::GeomBoundedCurve* bcurve = static_cast<Part::GeomBoundedCurve*>(mGeoList[id].get());

                        points.emplace_back(bcurve->getStartPoint().x, bcurve->getStartPoint().y);
                        points.emplace_back(bcurve->getEndPoint().x, bcurve->getEndPoint().y);
                    }

                    Intersector.Init(primaryCurve, secondaryCurve, 1.0e-12);

                    for (int i = 1; i <= Intersector.NbPoints(); i++) {
                        points.push_back(Intersector.Point(i));
                    }

                    if (Intersector.NbSegments() > 0) {
                        const Geom2dInt_GInter& gInter = Intersector.Intersector();
                        for (int i = 1; i <= gInter.NbSegments(); i++) {
                            const IntRes2d_IntersectionSegment& segm = gInter.Segment(i);
                            if (segm.HasFirstPoint()) {
                                const IntRes2d_IntersectionPoint& fp = segm.FirstPoint();
                                points.push_back(fp.Value());
                            }
                            if (segm.HasLastPoint()) {
                                const IntRes2d_IntersectionPoint& fp = segm.LastPoint();
                                points.push_back(fp.Value());
                            }
                        }
                    }

                    for (auto p : points) {
                        // get the parameter of the intersection point on the primary curve
                        Projector.Init(p, primaryCurve);

                        if (Projector.NbPoints() < 1
                            || Projector.LowerDistance() > Precision::Confusion()) {
                            continue;
                        }

                        double param = Projector.LowerDistanceParameter();

                        if (periodic) {
                            // transfer param into the interval (pickedParam-period pickedParam]
                            param = param - period * ceil((param - pickedParam) / period);
                            if (param > param1) {
                                param1 = param;
                                u1 = param1;
                                p1 = p;
                                geometryIndex1 = id;
                            }
                            param -= period;  // transfer param into the interval (pickedParam
                            // pickedParam+period]
                            if (param < param2) {
                                param2 = param;
                                u2 = param2;
                                p2 = p;
                                geometryIndex2 = id;
                            }
                        }
                        else if (param < pickedParam && param > param1) {
                            param1 = param;
                            p1 = p;
                            geometryIndex1 = id;
                            u1 = param1;
                        }
                        else if (param > pickedParam && param < param2) {
                            param2 = param;
                            u2 = param2;
                            p2 = p;
                            geometryIndex2 = id;
                        }
                    }
                }
            }
        }
        if (periodic) {
            // in case both points coincide, cancel the selection of one of both
            if (fabs(param2 - param1 - period) < 1e-10) {
                if (param2 - pickedParam >= pickedParam - param1) {
                    geometryIndex2 = -1;
                }
                else {
                    geometryIndex1 = -1;
                }
            }
        }

        //if (geometryIndex1 < 0 && geometryIndex2 < 0) {
        //    return false;
        //}

        if (geometryIndex1 >= 0) {
            intersect1 = Base::Vector3d(p1.X(), p1.Y(), 0.f);
        }
        else
        {
            const auto* geoAsCurve = static_cast<Part::GeomCurve*>(mGeoList[geometryIndex].get());
            u1 = geoAsCurve->getFirstParameter();
            intersect1 = geoAsCurve->value(u1);

        }
        if (geometryIndex2 >= 0) {
            intersect2 = Base::Vector3d(p2.X(), p2.Y(), 0.f);
        }
        else
        {
            const auto* geoAsCurve = static_cast<Part::GeomCurve*>(mGeoList[geometryIndex].get());
            u2 = geoAsCurve->getLastParameter();
            intersect2 = geoAsCurve->value(u2);
        }
        return true;
    }

    bool SketcherObj::isClosedCurve(const Part::Geometry* geo)
    {
        return (geo->is<Part::GeomCircle>()
            || geo->is<Part::GeomEllipse>()
            || (geo->is<Part::GeomBSplineCurve>()
                && static_cast<const Part::GeomBSplineCurve*>(geo)->isPeriodic()));
    }
    bool SketcherObj::trim(int GeoId, double u0, double u1,const Base::Vector3d& point0, const Base::Vector3d& point1)
    {
        const auto* geoAsCurve = static_cast<Part::GeomCurve*>(mGeoList[GeoId].get());
        std::vector<std::pair<double, double>> paramsOfNewGeos;
        paramsOfNewGeos.reserve(2);
        double firstParam = geoAsCurve->getFirstParameter();
        double lastParam = geoAsCurve->getLastParameter();
        double cut0Param{ u0 }, cut1Param{ u1 };
		bool isClosed = isClosedCurve(geoAsCurve);
        int numUndefs=0;
        bool cut0IsUndef = false;
        bool cut1IsUndef = false;
        if (!isClosed) {
			if (areParamsWithinApproximation(cut0Param, firstParam)) {
                numUndefs++;
				cut0IsUndef = true;
			}
			if (areParamsWithinApproximation(cut1Param, lastParam)) {
                numUndefs++;
				cut1IsUndef = true;
			}
        }
        if (numUndefs == 0 && arePointsWithinPrecision(point0,point1)) {
            // If both points are detected and are coincident, deletion is the only option.
            paramsOfNewGeos.clear();
        }
        else
        {
            paramsOfNewGeos.assign(2 - numUndefs, { firstParam, lastParam });
            if (isClosed) {
                paramsOfNewGeos.pop_back();
            }
            if (!cut0IsUndef) {
                paramsOfNewGeos.front().second = cut0Param;
            }
            if (!cut1IsUndef) {
                paramsOfNewGeos.back().first = cut1Param;
            }
        }

        std::vector<int> newIds;
        std::vector<std::unique_ptr<Part::Geometry>> newGeos;
        switch (paramsOfNewGeos.size()) {
            case 0: {
                {
					deleteGeometry(GeoId);
                }
                return true;
            }
            case 1: {
                newIds.push_back(GeoId);
                break;
            }
            case 2: {
                newIds.push_back(GeoId);
                newIds.push_back(mGeoList.size());
                break;
            }
            default: {
                return false;
            }
        }
        for (auto& [param1, param2] : paramsOfNewGeos) {
            Part::Geometry* newGeo = (geoAsCurve)->createArc(param1, param2);
            assert(newGeo);
			std::unique_ptr<Part::Geometry> newGeoPtr(newGeo);
            newGeos.push_back(std::move(newGeoPtr));
        }
        replaceGeometries({GeoId},newGeos);
        return true;
    }
    int SketcherObj::addSymmetric(const std::vector<int>& geoIdList, int refGeoId)
    {

        std::map<int, int> geoIdMap;
        std::map<int, bool> isStartEndInverted;
        std::vector<Part::Geometry*> symgeos= getSymmetric(geoIdList, geoIdMap, isStartEndInverted, refGeoId);
        addGeometry(symgeos);
        return geoIdList.size() - 1;
    }
    std::vector<Part::Geometry*> SketcherObj::getSymmetric(const std::vector<int>& geoIdList, std::map<int, int>& geoIdMap, std::map<int, bool>& isStartEndInverted, int refGeoId)
    {
        std::vector<Part::Geometry*> symmetricVals;
        
        int cgeoid = getHighestCurveIndex() + 1;

        const Part::Geometry* georef = getGeometry(refGeoId);
        if (!georef->is<Part::GeomLineSegment>()) {
            return {};
        }

        auto* refGeoLine = static_cast<const Part::GeomLineSegment*>(georef);
        // line
        Base::Vector3d refstart = refGeoLine->getStartPoint();
        Base::Vector3d vectline = refGeoLine->getEndPoint() - refstart;

        for (auto geoId : geoIdList) {
            const Part::Geometry* geo = getGeometry(geoId);
            Part::Geometry* geosym;

            geosym = geo->copy();

            // Handle Geometry
            if (geosym->is<Part::GeomLineSegment>()) {
                auto* geosymline = static_cast<Part::GeomLineSegment*>(geosym);
                Base::Vector3d sp = geosymline->getStartPoint();
                Base::Vector3d ep = geosymline->getEndPoint();

                geosymline->setPoints(
                    sp + 2.0 * (sp.Perpendicular(refGeoLine->getStartPoint(), vectline) - sp),
                    ep + 2.0 * (ep.Perpendicular(refGeoLine->getStartPoint(), vectline) - ep)
                );
                isStartEndInverted.insert(std::make_pair(geoId, false));
            }
            else if (geosym->is<Part::GeomCircle>()) {
                auto* geosymcircle = static_cast<Part::GeomCircle*>(geosym);
                Base::Vector3d cp = geosymcircle->getCenter();

                geosymcircle->setCenter(
                    cp + 2.0 * (cp.Perpendicular(refGeoLine->getStartPoint(), vectline) - cp)
                );
                isStartEndInverted.insert(std::make_pair(geoId, false));
            }
            else if (geosym->is<Part::GeomArcOfCircle>()) {
                auto* geoaoc = static_cast<Part::GeomArcOfCircle*>(geosym);
                Base::Vector3d sp = geoaoc->getStartPoint(true);
                Base::Vector3d ep = geoaoc->getEndPoint(true);
                Base::Vector3d cp = geoaoc->getCenter();

                Base::Vector3d ssp = sp
                    + 2.0 * (sp.Perpendicular(refGeoLine->getStartPoint(), vectline) - sp);
                Base::Vector3d sep = ep
                    + 2.0 * (ep.Perpendicular(refGeoLine->getStartPoint(), vectline) - ep);
                Base::Vector3d scp = cp
                    + 2.0 * (cp.Perpendicular(refGeoLine->getStartPoint(), vectline) - cp);

                double theta1 = Base::fmod(atan2(sep.y - scp.y, sep.x - scp.x), 2.f * 3.1415926535);
                double theta2 = Base::fmod(atan2(ssp.y - scp.y, ssp.x - scp.x), 2.f * 3.1415926535);

                geoaoc->setCenter(scp);
                geoaoc->setRange(theta1, theta2, true);
                isStartEndInverted.insert(std::make_pair(geoId, true));
            }
            else if (geosym->is<Part::GeomEllipse>()) {
                auto* geosymellipse = static_cast<Part::GeomEllipse*>(geosym);
                Base::Vector3d cp = geosymellipse->getCenter();

                Base::Vector3d majdir = geosymellipse->getMajorAxisDir();
                double majord = geosymellipse->getMajorRadius();
                double minord = geosymellipse->getMinorRadius();
                double df = sqrt(majord * majord - minord * minord);
                Base::Vector3d f1 = cp + df * majdir;

                Base::Vector3d sf1 = f1
                    + 2.0 * (f1.Perpendicular(refGeoLine->getStartPoint(), vectline) - f1);
                Base::Vector3d scp = cp
                    + 2.0 * (cp.Perpendicular(refGeoLine->getStartPoint(), vectline) - cp);

                geosymellipse->setMajorAxisDir(sf1 - scp);

                geosymellipse->setCenter(scp);
                isStartEndInverted.insert(std::make_pair(geoId, false));
            }
            else if (geosym->is<Part::GeomArcOfEllipse>()) {
                auto* geosymaoe = static_cast<Part::GeomArcOfEllipse*>(geosym);
                Base::Vector3d cp = geosymaoe->getCenter();

                Base::Vector3d majdir = geosymaoe->getMajorAxisDir();
                double majord = geosymaoe->getMajorRadius();
                double minord = geosymaoe->getMinorRadius();
                double df = sqrt(majord * majord - minord * minord);
                Base::Vector3d f1 = cp + df * majdir;

                Base::Vector3d sf1 = f1
                    + 2.0 * (f1.Perpendicular(refGeoLine->getStartPoint(), vectline) - f1);
                Base::Vector3d scp = cp
                    + 2.0 * (cp.Perpendicular(refGeoLine->getStartPoint(), vectline) - cp);

                geosymaoe->setMajorAxisDir(sf1 - scp);

                geosymaoe->setCenter(scp);

                double theta1, theta2;
                geosymaoe->getRange(theta1, theta2, true);
                theta1 = 2.0 * 3.1415926535 - theta1;
                theta2 = 2.0 * 3.1415926535 - theta2;
                std::swap(theta1, theta2);
                if (theta1 < 0) {
                    theta1 += 2.0 * 3.1415926535;
                    theta2 += 2.0 * 3.1415926535;
                }

                geosymaoe->setRange(theta1, theta2, true);
                isStartEndInverted.insert(std::make_pair(geoId, true));
            }
            else if (geosym->is<Part::GeomArcOfHyperbola>()) {
                auto* geosymaoe = static_cast<Part::GeomArcOfHyperbola*>(geosym);
                Base::Vector3d cp = geosymaoe->getCenter();

                Base::Vector3d majdir = geosymaoe->getMajorAxisDir();
                double majord = geosymaoe->getMajorRadius();
                double minord = geosymaoe->getMinorRadius();
                double df = sqrt(majord * majord + minord * minord);
                Base::Vector3d f1 = cp + df * majdir;

                Base::Vector3d sf1 = f1
                    + 2.0 * (f1.Perpendicular(refGeoLine->getStartPoint(), vectline) - f1);
                Base::Vector3d scp = cp
                    + 2.0 * (cp.Perpendicular(refGeoLine->getStartPoint(), vectline) - cp);

                geosymaoe->setMajorAxisDir(sf1 - scp);

                geosymaoe->setCenter(scp);

                double theta1, theta2;
                geosymaoe->getRange(theta1, theta2, true);
                theta1 = -theta1;
                theta2 = -theta2;
                std::swap(theta1, theta2);

                geosymaoe->setRange(theta1, theta2, true);
                isStartEndInverted.insert(std::make_pair(geoId, true));
            }
            else if (geosym->is<Part::GeomArcOfParabola>()) {
                auto* geosymaoe = static_cast<Part::GeomArcOfParabola*>(geosym);
                Base::Vector3d cp = geosymaoe->getCenter();

                Base::Vector3d f1 = geosymaoe->getFocus();

                Base::Vector3d sf1 = f1
                    + 2.0 * (f1.Perpendicular(refGeoLine->getStartPoint(), vectline) - f1);
                Base::Vector3d scp = cp
                    + 2.0 * (cp.Perpendicular(refGeoLine->getStartPoint(), vectline) - cp);

                geosymaoe->setXAxisDir(sf1 - scp);
                geosymaoe->setCenter(scp);

                double theta1, theta2;
                geosymaoe->getRange(theta1, theta2, true);
                theta1 = -theta1;
                theta2 = -theta2;
                std::swap(theta1, theta2);

                geosymaoe->setRange(theta1, theta2, true);
                isStartEndInverted.insert(std::make_pair(geoId, true));
            }
            else if (geosym->is<Part::GeomBSplineCurve>()) {
                auto* geosymbsp = static_cast<Part::GeomBSplineCurve*>(geosym);

                std::vector<Base::Vector3d> poles = geosymbsp->getPoles();

                for (auto& pole : poles) {
                    pole = pole
                        + 2.0 * (pole.Perpendicular(refGeoLine->getStartPoint(), vectline) - pole);
                }

                geosymbsp->setPoles(poles);

                isStartEndInverted.insert(std::make_pair(geoId, false));
            }
            else if (geosym->is<Part::GeomPoint>()) {
                auto* geosympoint = static_cast<Part::GeomPoint*>(geosym);
                Base::Vector3d cp = geosympoint->getPoint();

                geosympoint->setPoint(
                    cp + 2.0 * (cp.Perpendicular(refGeoLine->getStartPoint(), vectline) - cp)
                );
                isStartEndInverted.insert(std::make_pair(geoId, false));
            }
            else {
                CORE_ERROR("Unsupported Geometry!! Just copying it.\n");
                isStartEndInverted.insert(std::make_pair(geoId, false));
            }
            symmetricVals.push_back(geosym);
            geoIdMap.insert(std::make_pair(geoId, cgeoid));
            cgeoid++;
        }
        return symmetricVals;
    }
    Part::TopoShape SketcherObj::toShape() const
    {
        // Every geometry is turned into a *named* edge first, then the wires are
        // built from those: the names given here are the root of the naming chain
        // that later operations (prism, boolean, fillet, ...) extend, and they are
        // what keeps a reference to a sketch curve alive across a recompute.
        // Without them a reference can only mean "the n-th edge of the shape",
        // which changes as soon as anything upstream does.
        std::vector<Part::TopoShape> namedEdges;
        namedEdges.reserve(mGeoList.size());
        for (int geoId = 0; geoId < static_cast<int>(mGeoList.size()); ++geoId) {
            const Part::Geometry* geo = mGeoList[geoId].get();
            if (geo == nullptr || geo->getConstruction()) {
                continue;
            }
            Part::TopoShape shape(geo->toShape());
            if (shape.isNull() || shape.getShape().ShapeType() != TopAbs_EDGE) {
                continue;
            }
            nameSketchEdge(shape, geoId, geo);
            namedEdges.push_back(std::move(shape));
        }

        if (!namedEdges.empty()) {
            Part::TopoShape wired;
            wired.makeElementWires(namedEdges, Part::OpCodes::Sketch);
            if (!wired.isNull()) {
                LogTopoElementNames(wired, "sketch");
                wired.setTransform(planeTransform);
                return wired;
            }
            CORE_WARN(
                "[SketcherObj] makeElementWires() produced nothing for {0} edge(s); "
                "falling back to the plain edge chaining, whose names are lost",
                namedEdges.size());
        }

        // Fallback: the historical path, kept so a sketch whose edges cannot be
        // connected by the named builder still produces the shape it used to.
        Part::TopoShape result;
        std::list<TopoDS_Edge> edge_list;
        std::list<TopoDS_Wire> wires;
		for (const auto& geo : mGeoList) {
            if (!geo->getConstruction()) {
			    auto shape = geo->toShape();
			    if (shape.ShapeType() == TopAbs_EDGE) {
				    edge_list.push_back(TopoDS::Edge(shape));
			    }
            }
		}
        // Hint: Use ShapeAnalysis_FreeBounds::ConnectEdgesToWires() as an alternative
        // sort them together to wires
        while (!edge_list.empty()) {
            BRepBuilderAPI_MakeWire mkWire;
            // add and erase first edge
            mkWire.Add(edge_list.front());
            edge_list.pop_front();
            TopoDS_Wire new_wire = mkWire.Wire();  // current new wire
            // try to connect each edge to the wire, the wire is complete if no more edges are
            // connectible
            bool found = false;
            do {
                found = false;
                for (auto pE = edge_list.begin(); pE != edge_list.end(); ++pE) {
                    mkWire.Add(*pE);
                    if (mkWire.Error() != BRepBuilderAPI_DisconnectedWire) {
                        // edge added ==> remove it from list
                        found = true;
                        edge_list.erase(pE);
                        new_wire = mkWire.Wire();
                        break;
                    }
                }
            } while (found);

            // Fix any topological issues of the wire
            ShapeFix_Wire aFix;
            aFix.SetPrecision(Precision::Confusion());
            aFix.Load(new_wire);
            aFix.FixReorder();
            aFix.FixConnected();
            aFix.FixClosed();
            wires.push_back(aFix.Wire());
        }

        if (wires.size() == 1 ) {
            result = *wires.begin();
        }
        else if (wires.size() > 1 ) {
            BRep_Builder builder;
            TopoDS_Compound comp;
            builder.MakeCompound(comp);
            for (auto& wire : wires) {
                builder.Add(comp, wire);
            }
            result.setShape(comp);
        }
        result.setTransform(planeTransform);
        return result;
    }
    Base::Matrix4D SketcherObj::getplaneTransform() const
    {
        return planeTransform;
    }

    int SketcherObj::addExternalGeometry(
        Feature* p_source,
        const std::string& p_reference,
        bool p_intersection)
    {
        if (p_source == nullptr || p_reference.empty()) {
            return -1;
        }
        // The same sub-shape twice would only add a duplicate curve the solver has to
        // carry, and it usually means the button was pressed twice.
        for (int i = 0; i < static_cast<int>(mExternalGeometry.size()); ++i) {
            const ExternalGeometry& existing = mExternalGeometry[i];
            if (existing.source == p_source
                && existing.reference == p_reference
                && existing.intersection == p_intersection) {
                return i;
            }
        }

        // The ids the constraints hold count from the end of the external block, so
        // appending curves moves them: remember what the block looked like to be able
        // to move the constraints along with it.
        const std::vector<std::pair<int, int>> keysBefore = externalCurveKeys();

        ExternalGeometry external;
        external.source = p_source;
        external.reference = p_reference;
        external.intersection = p_intersection;
        mExternalGeometry.push_back(std::move(external));

        const int index = static_cast<int>(mExternalGeometry.size()) - 1;
        updateExternalGeometry();
        remapExternalReferences(keysBefore);
        const ExternalGeometry& added = mExternalGeometry[index];
        CORE_INFO(
            "[ExternalGeo] {0}: {1} '{2}' of {3} -> {4} curve(s){5}",
            getName(),
            p_intersection ? "sectioned" : "projected",
            p_reference,
            p_source->GetName(),
            added.geos.size(),
            added.missing ? " (missing)" : "");

        solve();
        return index;
    }

    void SketcherObj::clearExternalGeometry()
    {
        if (mExternalGeometry.empty()) {
            return;
        }
        const std::vector<std::pair<int, int>> keysBefore = externalCurveKeys();
        for (const ExternalGeometry& external : mExternalGeometry) {
            for (const std::unique_ptr<Part::Geometry>& geo : external.geos) {
                mGeoSegment.erase(geo.get());
            }
        }
        mExternalGeometry.clear();
        // Every reference is gone, so the constraints that named one go with them.
        remapExternalReferences(keysBefore);
        solve();
    }

	const SketcherObj::ExternalGeometry* SketcherObj::getExternalGeometry(int p_index) const
	{
		if (p_index < 0 || p_index >= static_cast<int>(mExternalGeometry.size())) {
			return nullptr;
		}
		return &mExternalGeometry[p_index];
	}

	int SketcherObj::getExternalCurveCount() const
	{
		int count = 0;
		for (const ExternalGeometry& external : mExternalGeometry) {
			count += static_cast<int>(external.geos.size());
		}
		return count;
	}

	int SketcherObj::getExternalGeoId(int p_index) const
	{
		return p_index - getExternalCurveCount();
	}

	int SketcherObj::getExternalCurveIndex(int p_geoId) const
	{
		const int count = getExternalCurveCount();
		if (count == 0 || p_geoId >= 0 || p_geoId == Sketcher::GeoEnum::GeoUndef) {
			return -1;
		}
		// The last curve of the block is -1, so the ids run the other way round.
		const int index = count + p_geoId;
		if (index < 0 || index >= count) {
			return -1;
		}
		return index;
	}

	const Part::Geometry* SketcherObj::getExternalCurve(int p_geoId) const
	{
		const int index = getExternalCurveIndex(p_geoId);
		if (index < 0) {
			return nullptr;
		}
		int current = 0;
		for (const ExternalGeometry& external : mExternalGeometry) {
			const int count = static_cast<int>(external.geos.size());
			if (index < current + count) {
				return external.geos[index - current].get();
			}
			current += count;
		}
		return nullptr;
	}

	bool SketcherObj::isExternalGeoId(int p_geoId) const
	{
		return getExternalCurveIndex(p_geoId) >= 0;
	}

	const Part::Geometry* SketcherObj::resolveGeometry(int p_geoId) const
	{
		if (p_geoId < 0) {
			return getExternalCurve(p_geoId);
		}
		return getGeometry(p_geoId);
	}

	std::vector<std::pair<int, int>> SketcherObj::externalCurveKeys() const
	{
		std::vector<std::pair<int, int>> keys;
		for (int i = 0; i < static_cast<int>(mExternalGeometry.size()); ++i) {
			const int count = static_cast<int>(mExternalGeometry[i].geos.size());
			for (int j = 0; j < count; ++j) {
				keys.emplace_back(i, j);
			}
		}
		return keys;
	}

	void SketcherObj::remapExternalReferences(
		const std::vector<std::pair<int, int>>& p_oldKeys)
	{
		const int oldCount = static_cast<int>(p_oldKeys.size());
		if (oldCount == 0) {
			return;  // nothing was projected, so no constraint can name it
		}
		const int newCount = getExternalCurveCount();

		// Where each curve of the old block sits now.
		std::map<std::pair<int, int>, int> position;
		int index = 0;
		for (int i = 0; i < static_cast<int>(mExternalGeometry.size()); ++i) {
			const int count = static_cast<int>(mExternalGeometry[i].geos.size());
			for (int j = 0; j < count; ++j, ++index) {
				position.emplace(std::make_pair(i, j), index);
			}
		}

		std::vector<Sketcher::Constraint*> kept;
		kept.reserve(mConstraintList.size());
		int dropped = 0;
		for (Sketcher::Constraint* constraint : mConstraintList) {
			if (constraint == nullptr) {
				continue;
			}
			bool dead = false;
			for (int* geoId : { &constraint->First, &constraint->Second, &constraint->Third }) {
				if (*geoId >= 0 || *geoId == Sketcher::GeoEnum::GeoUndef) {
					continue;  // an internal element, or an element that is not set
				}
				const int oldIndex = *geoId + oldCount;
				if (oldIndex < 0) {
					continue;  // more negative than the block: not one of its curves
				}
				const auto found = position.find(p_oldKeys[oldIndex]);
				if (found == position.end()) {
					dead = true;  // the curve it referenced is gone
					break;
				}
				*geoId = found->second - newCount;
			}
			if (dead) {
				delete constraint;
				++dropped;
				continue;
			}
			kept.push_back(constraint);
		}
		mConstraintList.swap(kept);
		if (dropped > 0) {
			CORE_INFO(
				"[ExternalGeo] {0}: dropped {1} constraint(s) whose reference is gone",
				getName(),
				dropped);
		}
	}

	void SketcherObj::setExternalGeometryMode(bool p_on)
	{
		if (m_externalGeometryMode == p_on) {
			return;
		}
		m_externalGeometryMode = p_on;
		if (p_on) {
			// The picks of this mode belong to the cursor, like the draw handlers: a
			// preselect left over from the last tool would only be in the way.
			preSelectGeoId = { NoGeoId, PointPos::none };
			selectState = Stop;
			CORE_INFO(
				"[ExternalGeo] {0}: click a face or an edge of another feature to "
				"{1}; Escape or the toolbar button ends the mode",
				getName(),
				m_externalGeometryIntersection
					? "cut it with the sketch plane"
					: "project it into the sketch plane");
		}
	}

	bool SketcherObj::removeExternalGeometry(int p_index)
	{
		if (p_index < 0 || p_index >= static_cast<int>(mExternalGeometry.size())) {
			return false;
		}
		const ExternalGeometry& external = mExternalGeometry[p_index];
		const std::vector<std::pair<int, int>> keysBefore = externalCurveKeys();
		CORE_INFO(
			"[ExternalGeo] {0}: dropped '{1}' of {2}",
			getName(),
			external.reference,
			external.source ? external.source->GetName() : "<null>");
		for (const std::unique_ptr<Part::Geometry>& geo : external.geos) {
			mGeoSegment.erase(geo.get());
		}
		mExternalGeometry.erase(mExternalGeometry.begin() + p_index);
		// Constraints on this reference die with it; the ones on the other curves are
		// moved to the ids they have now.
		remapExternalReferences(keysBefore);
		solve();
		return true;
	}

	bool SketcherObj::pickExternalGeometry()
	{
		auto pickingResult = m_sceneView->GetPickResult();
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
				"[ExternalGeo] {0}: '{1}' does not belong to a feature, so it cannot "
				"be referenced",
				getName(), actor->GetName());
			return false;
		}
		if (source == m_ownerFeature) {
			CORE_WARN(
				"[ExternalGeo] {0}: '{1}' belongs to the sketch itself",
				getName(), reference);
			return false;
		}
		return addExternalGeometry(source, reference, m_externalGeometryIntersection) >= 0;
	}

	void SketcherObj::updateExternalGeometry()
	{
		// Dropping the projections has to drop their sampling with them: the cache is
		// keyed by address, so an entry of a freed geometry would outlive it - and a
		// later geometry allocated at the same address would be drawn from the wrong
		// curve.
		const auto dropProjections = [this](ExternalGeometry& p_external) {
			for (const std::unique_ptr<Part::Geometry>& geo : p_external.geos) {
				mGeoSegment.erase(geo.get());
			}
			p_external.geos.clear();
		};

		for (ExternalGeometry& external : mExternalGeometry) {
			if (external.source == nullptr) {
				external.missing = true;
				dropProjections(external);
				continue;
			}
			auto comp
				= external.source->GetComponent<Core::ECS::Components::CTopoShape>();
			if (comp == nullptr) {
				external.missing = true;
				dropProjections(external);
				continue;
			}

			// Nothing to do while the source still holds the shape the projection was
			// taken from: solve() runs on every mouse move of a drag, and resolving and
			// projecting again each step would only cost time without changing anything.
			const TopoDS_Shape& sourceShape = comp->GetTopoShape().getShape();
			if (!external.dirty && !sourceShape.IsNull()
				&& external.sourceShape.IsSame(sourceShape)) {
				continue;
			}

			// Resolve against the source as it is *now*: by name first, so a recompute
			// of the source cannot move the reference onto another sub-shape.
			Part::TopoShape subShape
				= ResolveSubShapeRef(*external.source, external.reference, external.names);
			// The projections are about to be replaced.
			dropProjections(external);
			// The source shape is remembered either way: a reference that does not
			// resolve (yet) must not be retried - and reported - on every solver step
			// of a drag.
			external.sourceShape = sourceShape;
			external.dirty = false;
			if (subShape.isNull()) {
				external.missing = true;
				CORE_WARN(
					"[ExternalGeo] {0}: '{1}' of {2} cannot be resolved; the reference "
					"is kept but draws nothing",
					getName(),
					external.reference,
					external.source->GetName());
				continue;
			}

			external.missing = false;
			external.geos
				= projectIntoSketch(subShape.getShape(), mPlane, external.intersection);
			// The curves are sampled here, like the sketch samples its own when they are
			// added: the drawing, the hit test and the snapping all read that one cache,
			// so it has to exist as soon as the curves do - not on the first frame that
			// happens to draw them.
			for (const std::unique_ptr<Part::Geometry>& geo : external.geos) {
				if (geo != nullptr) {
					mGeoSegment[geo.get()] = getCurveSegment(geo.get());
				}
			}
			if (external.geos.empty()) {
				CORE_WARN(
					"[ExternalGeo] {0}: '{1}' of {2} projected to nothing",
					getName(),
					external.reference,
					external.source->GetName());
			}
        }
    }

    void SketcherObj::retrieveSolverDiagnostics()
    {
        lastHasConflict = solvedSketch.hasConflicts();
        lastHasRedundancies = solvedSketch.hasRedundancies();
        lastHasPartialRedundancies = solvedSketch.hasPartialRedundancies();
        lastHasMalformedConstraints = solvedSketch.hasMalformedConstraints();
        lastConflicting = solvedSketch.getConflicting();
        lastRedundant = solvedSketch.getRedundant();
        lastPartiallyRedundant = solvedSketch.getPartiallyRedundant();
        lastMalformedConstraints = solvedSketch.getMalformedConstraints();
    }
 
    Base::Matrix4D SketcherObj::updateTransform() const
    {
        Base::Matrix4D ret;
        ret = Base::Matrix4D(
            mPlane.xAxis.x, mPlane.yAxis.x, mPlane.normal.x, mPlane.origin.x,
            mPlane.xAxis.y, mPlane.yAxis.y, mPlane.normal.y, mPlane.origin.y,
            mPlane.xAxis.z, mPlane.yAxis.z, mPlane.normal.z, mPlane.origin.z,
            0.0, 0.0, 0.0, 1.0
        );
        return ret;
    }


}
