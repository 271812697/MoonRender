#include "Sketcher/SketcherObj.h"
#include "Sketcher/SketcheTool2D.h"
#include "Geometry.h"

#include "base/Tools.h"
#include "core/log.h"
#include "core/TopoNameDebug.h"

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
#include <algorithm>
#include <cmath>
#include <limits>
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

 
	SketcherObj::SketcherObj()
    {
        // The axes belong to the sketch from the start: a constraint can always name
        // the origin or one of the axes, without the user having to bring them in as
        // external geometry first (see ensureAxisGeometry).
        ensureAxisGeometry();
    }

    void SketcherObj::ensureAxisGeometry()
    {
        if (mExternalAxes[0] != nullptr && mExternalAxes[1] != nullptr) {
            return;
        }
        // The axes are lines of the sketch plane. How far they reach only matters for
        // drawing and picking - the solver works with the line through their two
        // points - but they have to *start* at the origin: the root point is the start
        // point of the horizontal axis, which is the convention GeoEnum documents
        // (RtPnt and HAxis share the id -1).
        //
        // The span matches what the grid draws its own axis lines over, so what the
        // user sees and what they can click on are the same lines in the positive
        // quadrant.
        constexpr double extent = 500.0;

        auto vertical = std::make_unique<Part::GeomLineSegment>();
        vertical->setPoints(
            Base::Vector3d(0.0, 0.0, 0.0),
            Base::Vector3d(0.0, extent, 0.0));
        // Construction, i.e. reference geometry: it is never part of the sketch's wire
        // shape (the external flag alone already keeps it out of that).
        vertical->setConstruction(true);

        auto horizontal = std::make_unique<Part::GeomLineSegment>();
        horizontal->setPoints(
            Base::Vector3d(0.0, 0.0, 0.0),
            Base::Vector3d(extent, 0.0, 0.0));
        horizontal->setConstruction(true);

        mExternalAxes[0] = std::move(vertical);
        mExternalAxes[1] = std::move(horizontal);

        // Sampled right away, like every other curve of the sketch: the hit test and
        // the snapping only look at curves that already have a cache entry, and the
        // axes have to be selectable from the first frame on.
        for (const std::unique_ptr<Part::Geometry>& axis : mExternalAxes) {
            mGeoSegment[axis.get()] = getCurveSegment(axis.get());
        }
    }
    SketcherObj::~SketcherObj()
    {
        for (Sketcher::Constraint* c : mConstraintList) {
            delete c;
        }
        mConstraintList.clear();
    }
    void SketcherObj::makeDone()
    {
        doneWireShape = toShape();
        if (!doneWireShape.isEmpty()) {
            try
            {
                 doneFaceShape = doneWireShape.makeElementFace(nullptr, "Part::FaceMakerBullseye");
            }
            catch (Part::NullShapeException&e)
            {
                CORE_ERROR("the shape is null ,can't make a face");
            }
            catch (Base::ValueError&e) {
                CORE_ERROR(e.what());
            }
        }
        // Leaving the edit session (camera, tools) is the widget's business: it is the
        // one that calls this when the user commits the sketch, see
        // SketcherObjWidget::finishEdit().
    }

    int SketcherObj::solve(bool updateGeoAfterSolving)
    {
        ensureAxisGeometry();
        //Reset
        solvedSketch.resetInitMove();
        //Set Up geometry and contraint
        std::vector<Part::Geometry*> GeoList;
        for (int i = 0; i < mGeoList.size(); i++) {
            GeoList.push_back(mGeoList[i].get());
        }
        // External geometry goes last: setUpSketch() takes the trailing extGeoCount
        // entries as blocked, i.e. as parameters the sketch may use but never move.
        // The user's references come first and the two axes after them, so the axes
        // are the last entries of the whole list - which is what gives them the fixed
        // ids -1 and -2 the constraints and the UI expect (see getExternalCurveCount).
        int externalCount = 0;
        for (const std::unique_ptr<Part::Geometry>& geo : mExternalGeoList) {
            GeoList.push_back(geo.get());
            ++externalCount;
        }
        for (const std::unique_ptr<Part::Geometry>& axis : mExternalAxes) {
            if (axis != nullptr) {
                GeoList.push_back(axis.get());
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
            takeSolvedGeometry();
        }
      
        return err;
    }
 
    // ---------------------------------------------------------------------------
    // The selection: what the widgets pick, kept with the sketch so every one of
    // them sees the same thing (see the header).
    // ---------------------------------------------------------------------------
    std::vector<int> SketcherObj::getSelectIds() const
    {
        std::vector<int> ids;
        ids.reserve(selectIds.size());
        for (const SelectGeoId& sel : selectIds) {
            ids.push_back(sel.GeoId);
        }
        return ids;
    }
    bool SketcherObj::isSelected(const SelectGeoId& p_geoId) const
    {
        for (const SelectGeoId& sel : selectIds) {
            if (sel.GeoId == p_geoId.GeoId && sel.pointPos == p_geoId.pointPos) {
                return true;
            }
        }
        return false;
    }
    void SketcherObj::addSelect(const SelectGeoId& p_geoId)
    {
        if (!isSelected(p_geoId)) {
            selectIds.push_back(p_geoId);
        }
    }
    void SketcherObj::removeSelect(const std::vector<int>& idList)
    {
        // What stays keeps its order; the entries that name a dropped curve go.
        size_t kept = 0;
        for (size_t i = 0; i < selectIds.size(); ++i) {
            bool drop = false;
            for (int id : idList) {
                if (selectIds[i].GeoId == id) {
                    drop = true;
                    break;
                }
            }
            if (!drop) {
                selectIds[kept++] = selectIds[i];
            }
        }
        selectIds.resize(kept);
    }
    void SketcherObj::selectGeo(int geoId)
    {
        clearSelect();
        if (geoId >= 0 && geoId < static_cast<int>(mGeoList.size())) {
            addSelect({ geoId, PointPos::none });
        }
    }
    // ---------------------------------------------------------------------------
    // What a dimension tool hands over to the widget that draws the annotation, and
    // the flag that says the sketch is being edited (see the header).
    // ---------------------------------------------------------------------------
    void SketcherObj::setAnnotationDropPoint(const Sketcher::Constraint* p_constraint, const Base::Vector2d& p_sketchPos)
    {
        if (p_constraint != nullptr) {
            m_annotationDrops[p_constraint] = p_sketchPos;
        }
    }
    void SketcherObj::clearAnnotationDropPoint(const Sketcher::Constraint* p_constraint)
    {
        m_annotationDrops.erase(p_constraint);
    }
    void SketcherObj::pruneAnnotationDrops()
    {
        for (auto it = m_annotationDrops.begin(); it != m_annotationDrops.end();) {
            const Sketcher::Constraint* c = it->first;
            bool alive = false;
            for (int i = 0; i < static_cast<int>(mConstraintList.size()); ++i) {
                if (mConstraintList[i] == c) {
                    alive = true;
                    break;
                }
            }
            if (alive) {
                ++it;
            }
            else {
                it = m_annotationDrops.erase(it);
            }
        }
    }
    void SketcherObj::takeSolvedGeometry()
    {
        // The solver hands out clones of what it solved and the sketch keeps its own
        // copies, so the sampling cache of the curves that are replaced goes first.
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
        else {
            // An empty sketch is fine; a sketch with curves that produced no edge is
            // not, and without this it would only show up much later as a feature that
            // has nothing to build on.
            CORE_WARN(
                "[SketcherObj] {0}: none of the {1} curve(s) produced an edge",
                getName(),
                mGeoList.size());
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


    int SketcherObj::addExternalGeometry(std::unique_ptr<Part::Geometry> p_geo)
    {
        if (p_geo == nullptr) {
            return NoGeoId;
        }
        // The ids the constraints hold count from the end of the external block, so
        // appending to it moves them: remember what the block held to be able to move
        // the constraints along with it.
        const std::vector<Part::Geometry*> before = externalGeometryPointers();
        mExternalGeoList.push_back(std::move(p_geo));

        Part::Geometry* geo = mExternalGeoList.back().get();
        // Sampled as soon as it exists, like the sketch samples its own geometry when
        // it is added: the drawing, the hit test and the snapping all read that one
        // cache, so it must not wait for the first frame that happens to draw it.
        mGeoSegment[geo] = getCurveSegment(geo);

        remapExternalReferences(before);
        solve();
        return getExternalGeoId(static_cast<int>(mExternalGeoList.size()) - 1);
    }

    int SketcherObj::addExternalGeometry(std::vector<std::unique_ptr<Part::Geometry>>&& p_geos)
    {
        if (p_geos.empty()) {
            return NoGeoId;
        }
        const std::vector<Part::Geometry*> before = externalGeometryPointers();
        int firstGeoId = NoGeoId;
        for (std::unique_ptr<Part::Geometry>& geo : p_geos) {
            if (geo == nullptr) {
                continue;
            }
            mExternalGeoList.push_back(std::move(geo));
            Part::Geometry* added = mExternalGeoList.back().get();
            mGeoSegment[added] = getCurveSegment(added);
            if (firstGeoId == NoGeoId) {
                firstGeoId = getExternalGeoId(static_cast<int>(mExternalGeoList.size()) - 1);
            }
        }
        if (firstGeoId == NoGeoId) {
            return NoGeoId;  // everything in the list was null
        }
        remapExternalReferences(before);
        solve();
        return firstGeoId;
    }

    bool SketcherObj::removeExternalGeometry(int p_index)
    {
        if (p_index < 0 || p_index >= static_cast<int>(mExternalGeoList.size())) {
            return false;
        }
        const std::vector<Part::Geometry*> before = externalGeometryPointers();
        // Dropping the curve has to drop its sampling with it: the cache is keyed by
        // address, so an entry of a freed geometry would outlive it - and a later
        // geometry allocated at the same address would be drawn from the wrong curve.
        mGeoSegment.erase(mExternalGeoList[p_index].get());
        CORE_INFO(
            "[ExternalGeo] {0}: dropped external curve {1}",
            getName(),
            getExternalGeoId(p_index));
        mExternalGeoList.erase(mExternalGeoList.begin() + p_index);
        // The constraints on this curve die with it; the ones on the other curves are
        // moved to the ids they have now.
        remapExternalReferences(before);
        solve();
        return true;
    }

    void SketcherObj::clearExternalGeometry()
    {
        if (mExternalGeoList.empty()) {
            return;
        }
        const std::vector<Part::Geometry*> before = externalGeometryPointers();
        for (const std::unique_ptr<Part::Geometry>& geo : mExternalGeoList) {
            mGeoSegment.erase(geo.get());
        }
        mExternalGeoList.clear();
        // Every curve is gone, so the constraints that named one go with them.
        remapExternalReferences(before);
        solve();
    }

    Part::Geometry* SketcherObj::getExternalGeometry(int p_index)
    {
        if (p_index < 0 || p_index >= static_cast<int>(mExternalGeoList.size())) {
            return nullptr;
        }
        return mExternalGeoList[p_index].get();
    }

    const Part::Geometry* SketcherObj::getExternalGeometry(int p_index) const
    {
        if (p_index < 0 || p_index >= static_cast<int>(mExternalGeoList.size())) {
            return nullptr;
        }
        return mExternalGeoList[p_index].get();
    }

    int SketcherObj::getExternalCurveCount() const
    {
        // The user's references, then the two axes - the axes are part of the block
        // (they are drawn, picked and constrained to like any other curve) even though
        // they are not in the list the user can add to and remove from.
        int count = static_cast<int>(mExternalGeoList.size());
        for (const std::unique_ptr<Part::Geometry>& axis : mExternalAxes) {
            if (axis != nullptr) {
                ++count;
            }
        }
        return count;
    }

    int SketcherObj::getExternalGeoId(int p_index) const
    {
        const int count = getExternalCurveCount();
        if (p_index < 0 || p_index >= count) {
            return NoGeoId;
        }
        return p_index - count;
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
        if (index < static_cast<int>(mExternalGeoList.size())) {
            return mExternalGeoList[index].get();
        }
        // The tail of the block: the vertical axis and then the horizontal one, whose
        // start point is the root point.
        return mExternalAxes[index - static_cast<int>(mExternalGeoList.size())].get();
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

    SketcherObj::MeasuredAngle SketcherObj::measureAngleBetweenLineEnds(
        const Base::Vector2d& p_firstStart,
        const Base::Vector2d& p_firstEnd,
        const Base::Vector2d& p_secondStart,
        const Base::Vector2d& p_secondEnd,
        PointPos p_firstPos,
        PointPos p_secondPos)
    {
        MeasuredAngle measured;
        const Base::Vector2d& firstStart = p_firstStart;
        const Base::Vector2d& firstEnd = p_firstEnd;
        const Base::Vector2d& secondStart = p_secondStart;
        const Base::Vector2d& secondEnd = p_secondEnd;
        const Base::Vector2d firstDir = firstEnd - firstStart;
        const Base::Vector2d secondDir = secondEnd - secondStart;
        const double determinant = firstDir.x * secondDir.y - firstDir.y * secondDir.x;

        const bool needFirstEnd = p_firstPos == PointPos::none;
        const bool needSecondEnd = p_secondPos == PointPos::none;
        if ((needFirstEnd || needSecondEnd) && std::abs(determinant) > 1.0e-9) {
            // Where the two lines meet; the angle is measured from the end of each
            // line that sits closest to that corner.
            const Base::Vector2d delta = secondStart - firstStart;
            const double along = (delta.x * secondDir.y - delta.y * secondDir.x) / determinant;
            const Base::Vector2d corner = firstStart + firstDir * along;
            if (needFirstEnd) {
                measured.firstPos = (corner - firstStart).Length() < (corner - firstEnd).Length()
                    ? PointPos::start
                    : PointPos::end;
            }
            if (needSecondEnd) {
                measured.secondPos
                    = (corner - secondStart).Length() < (corner - secondEnd).Length()
                    ? PointPos::start
                    : PointPos::end;
            }
        }
        else {
            // Parallel: the closest pair of ends plays the same part. A pair that is
            // not collinear has no angle between it at all, which the caller refuses
            // exactly like the sketcher does.
            if (needFirstEnd || needSecondEnd) {
                double closest = std::numeric_limits<double>::max();
                for (int i = 0; i < 2; ++i) {
                    for (int j = 0; j < 2; ++j) {
                        const Base::Vector2d& first = i == 0 ? firstStart : firstEnd;
                        const Base::Vector2d& second = j == 0 ? secondStart : secondEnd;
                        const double distance = (first - second).Length();
                        if (distance < closest) {
                            closest = distance;
                            if (needFirstEnd) {
                                measured.firstPos
                                    = i == 0 ? PointPos::start : PointPos::end;
                            }
                            if (needSecondEnd) {
                                measured.secondPos
                                    = j == 0 ? PointPos::start : PointPos::end;
                            }
                        }
                    }
                }
                if (closest > Precision::Confusion()) {
                    return measured;
                }
            }
        }
        if (p_firstPos != PointPos::none) {
            measured.firstPos = p_firstPos;
        }
        if (p_secondPos != PointPos::none) {
            measured.secondPos = p_secondPos;
        }

        // The two directions the angle runs between: each points away from the end
        // that was picked above.
        const Base::Vector2d dir1
            = (measured.firstPos == PointPos::start ? 1.0 : -1.0) * firstDir;
        const Base::Vector2d dir2
            = (measured.secondPos == PointPos::start ? 1.0 : -1.0) * secondDir;

        double radians = std::atan2(
            dir1.x * dir2.y - dir1.y * dir2.x,
            dir1.x * dir2.x + dir1.y * dir2.y
        );
        if (radians < 0.0) {
            // Kept positive: the two lines swap places instead, which is what makes
            // the two supplements read out of the same pair of lines.
            radians = -radians;
            std::swap(measured.firstPos, measured.secondPos);
            measured.swapped = true;
        }
        measured.radians = radians;
        measured.usable = true;
        return measured;
    }

    SketcherObj::MeasuredAngle SketcherObj::measureAngleBetweenLines(
        int p_firstGeoId,
        int p_secondGeoId,
        PointPos p_firstPos,
        PointPos p_secondPos) const
    {
        const auto lineEnds = [this](int p_geoId, Base::Vector2d& p_start, Base::Vector2d& p_end) {
            const Part::Geometry* geo = resolveGeometry(p_geoId);
            if (geo == nullptr || !geo->is<Part::GeomLineSegment>()) {
                return false;
            }
            const auto* line = static_cast<const Part::GeomLineSegment*>(geo);
            const Base::Vector3d start = line->getStartPoint();
            const Base::Vector3d end = line->getEndPoint();
            p_start = Base::Vector2d(start.x, start.y);
            p_end = Base::Vector2d(end.x, end.y);
            return true;
        };

        Base::Vector2d firstStart;
        Base::Vector2d firstEnd;
        Base::Vector2d secondStart;
        Base::Vector2d secondEnd;
        if (!lineEnds(p_firstGeoId, firstStart, firstEnd)
            || !lineEnds(p_secondGeoId, secondStart, secondEnd)) {
            return MeasuredAngle();
        }

        MeasuredAngle measured = measureAngleBetweenLineEnds(
            firstStart,
            firstEnd,
            secondStart,
            secondEnd,
            p_firstPos,
            p_secondPos
        );
        if (!measured.usable) {
            return measured;
        }
        // The measured order, not the order the two lines were handed in.
        measured.firstGeoId = measured.swapped ? p_secondGeoId : p_firstGeoId;
        measured.secondGeoId = measured.swapped ? p_firstGeoId : p_secondGeoId;
        return measured;
    }

    std::vector<Part::Geometry*> SketcherObj::externalGeometryPointers() const
    {
        std::vector<Part::Geometry*> pointers;
        pointers.reserve(mExternalGeoList.size());
        for (const std::unique_ptr<Part::Geometry>& geo : mExternalGeoList) {
            pointers.push_back(geo.get());
        }
        return pointers;
    }

    void SketcherObj::remapExternalReferences(const std::vector<Part::Geometry*>& p_before)
    {
        const int oldCount = static_cast<int>(p_before.size());
        if (oldCount == 0) {
            return;  // nothing was projected, so no constraint can name it
        }
        const int newCount = getExternalCurveCount();
        // The references sit below the two axes in the external block, so their ids
        // start at -3 and the index of a curve is its id plus the size of the whole
        // block - the axes included.
        const int axisCount = newCount - static_cast<int>(mExternalGeoList.size());
        const int oldBlockSize = oldCount + axisCount;
        const int newBlockSize = newCount;

        // Where each curve of the old block sits now. The curves themselves are the
        // identity a constraint is moved by: a curve keeps its place when one next to
        // it is dropped, and a geoId is only ever a position counted from the end.
        std::map<const Part::Geometry*, int> position;
        for (int i = 0; i < static_cast<int>(mExternalGeoList.size()); ++i) {
            position.emplace(mExternalGeoList[i].get(), i);
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
                const int oldIndex = *geoId + oldBlockSize;
                if (oldIndex < 0 || oldIndex >= oldCount) {
                    continue;  // an axis (-1, -2), or below the block: not a reference
                }
                const auto found = position.find(p_before[oldIndex]);
                if (found == position.end()) {
                    dead = true;  // the curve it referenced is gone
                    break;
                }
                *geoId = found->second - newBlockSize;
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
            ++m_constraintRevision;
            pruneAnnotationDrops();
            CORE_INFO(
                "[ExternalGeo] {0}: dropped {1} constraint(s) whose reference is gone",
                getName(),
                dropped);
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



    // ---------------------------------------------------------------------------
    // Curves, external references and constraints: the operations on the sketch
    // data. They used to sit in SketcherInteraction.cpp next to the interaction;
    // they are the model half, so they live with the class they belong to.
    // ---------------------------------------------------------------------------
    int SketcherObj::addGeometry(std::unique_ptr<Part::Geometry>& ptr)
    {
        Part::Geometry* geo = ptr.get();
        mGeoSegment[geo] = getCurveSegment(geo);
        mGeoList.push_back(std::move(ptr));
        return mGeoList.size() - 1;
    }
    int SketcherObj::addGeometry(Part::Geometry* curve)
    {
        std::unique_ptr<Part::Geometry>temp(curve->copy());
        return addGeometry(temp);
    }
    void SketcherObj::addGeometry(const std::vector<Part::Geometry*>& curveList)
    {
        for (int i = 0; i < curveList.size(); i++) {
            std::unique_ptr<Part::Geometry> temp(curveList[i]->copy());
            addGeometry(temp);
        }
    }
    Part::Geometry* SketcherObj::getGeometry(int GeoId)
    {
        if (GeoId >= 0 && GeoId < mGeoList.size()) {
            return mGeoList[GeoId].get();
        }
        return nullptr;
    }
    const Part::Geometry* SketcherObj::getGeometry(int GeoId) const
    {
        if (GeoId >= 0 && GeoId < static_cast<int>(mGeoList.size())) {
            return mGeoList[GeoId].get();
        }
        return nullptr;
    }
    int SketcherObj::getHighestCurveIndex()
    {
        return mGeoList.size() - 1;
    }
    void SketcherObj::deleteGeometry(int GeoId)
    {
        if (GeoId < mGeoList.size()) {
            auto it = mGeoList.begin();
            std::advance(it, GeoId);
            mGeoSegment.erase((*it).get());
            mGeoList.erase(it);
        }
    }
    void SketcherObj::deleteGeometries(const std::vector<int>& GeoIds)
    {
        if (GeoIds.size() == 0) {
            return;
        }
        const int oldSize = static_cast<int>(mGeoList.size());
        std::vector<int> deletePos(oldSize, 0);
        for (int i = 0;i < GeoIds.size();i++) {
            if (GeoIds[i] >= 0 && GeoIds[i] < oldSize) {
                deletePos[GeoIds[i]] = 1;
            }
        }
        // Construction aids (e.g. rounded-rectangle corner points) that are no
        // longer referenced by any surviving constraint become garbage after
        // this deletion; remove them together with the selected geometry so
        // they cannot be left behind as undeletable points.
        std::vector<char> survivorReferenced(oldSize, 0);
        auto referencesDeleted = [&](int geoId) {
            return geoId >= 0 && geoId < oldSize && deletePos[geoId];
        };
        for (Sketcher::Constraint* c : mConstraintList) {
            if (referencesDeleted(c->First) || referencesDeleted(c->Second)
                || referencesDeleted(c->Third)) {
                continue;  // this constraint dies with the selection
            }
            if (c->First >= 0 && c->First < oldSize) {
                survivorReferenced[c->First] = 1;
            }
            if (c->Second >= 0 && c->Second < oldSize) {
                survivorReferenced[c->Second] = 1;
            }
            if (c->Third >= 0 && c->Third < oldSize) {
                survivorReferenced[c->Third] = 1;
            }
        }
        for (int oldId : mConstructionGeoIds) {
            if (oldId >= 0 && oldId < oldSize && !deletePos[oldId]
                && !survivorReferenced[oldId]) {
                deletePos[oldId] = 1;
            }
        }
        // Map every surviving old index to its new index after removal.
        std::vector<int> newIndex(oldSize, -1);
        int nextIndex = 0;
        for (int i = 0; i < oldSize; ++i) {
            if (!deletePos[i]) {
                newIndex[i] = nextIndex++;
            }
        }
        std::set<int> remappedHidden;
        for (int oldId : mHiddenGeoIds) {
            if (oldId >= 0 && oldId < oldSize && !deletePos[oldId]) {
                remappedHidden.insert(newIndex[oldId]);
            }
        }
        mHiddenGeoIds.swap(remappedHidden);
        // Keep construction markers attached to their (surviving) geometry
        // after the index remap.
        std::set<int> remappedConstruction;
        for (int oldId : mConstructionGeoIds) {
            if (oldId >= 0 && oldId < oldSize && !deletePos[oldId]) {
                remappedConstruction.insert(newIndex[oldId]);
            }
        }
        mConstructionGeoIds.swap(remappedConstruction);

		auto it = mGeoList.begin();
        int index = 0;
        while (it != mGeoList.end()) {
            if (deletePos[index] == 1) {
                mGeoSegment.erase((*it).get());
                it = mGeoList.erase(it);
                if (index >= oldSize) {
                    break;
                }
            }
            else {
                it++;
            }
            index++;
        }

        // FreeCAD deletes every constraint that references a removed geometry
        // and shifts the GeoIds of all constraints after the deletion point.
        auto remapGeoId = [&](int& geoId) -> bool {
            if (geoId >= 0 && geoId < oldSize) {
                if (deletePos[geoId]) {
                    return false;  // constraint refers to a deleted element
                }
                geoId = newIndex[geoId];
            }
            return true;
            };

        std::vector<Sketcher::Constraint*> keptConstraints;
        keptConstraints.reserve(mConstraintList.size());
        for (Sketcher::Constraint* c : mConstraintList) {
            bool keep = remapGeoId(c->First);
            keep = keep && remapGeoId(c->Second);
            keep = keep && remapGeoId(c->Third);
            if (keep) {
                keptConstraints.push_back(c);
            }
            else {
                delete c;
            }
        }
        mConstraintList = std::move(keptConstraints);
        // Deleted constraints invalidate the label overlay bookkeeping, which
        // is keyed by constraint pointer.
        // Constraints can die with the geometry they named; whoever edits the sketch
        // notices that through the revision rather than being told.
        ++m_constraintRevision;
        pruneAnnotationDrops();
    }
    void SketcherObj::replaceGeometry(int oldGeoId, std::unique_ptr<Part::Geometry>& newGeo)
    {
        if (oldGeoId < mGeoList.size()) {
            mGeoSegment.erase(mGeoList[oldGeoId].get());
            mGeoList[oldGeoId] = std::move((newGeo));
            mGeoSegment[mGeoList[oldGeoId].get()] = getCurveSegment(mGeoList[oldGeoId].get());
        }
    }
    void SketcherObj::replaceGeometries(const std::vector<int>& oldGeoIds, std::vector<std::unique_ptr<Part::Geometry>>& newGeos)
    {
        int i = 0;
        for (;i < oldGeoIds.size() && i < newGeos.size();i++) {
            int oldGeoId = oldGeoIds[i];
            if (oldGeoId < mGeoList.size()) {
                replaceGeometry(oldGeoId, newGeos[i]);
            }
        }
        for (;i < newGeos.size();i++) {
            addGeometry(newGeos[i]);
        }
    }
    int SketcherObj::addConstraint(const Sketcher::Constraint* constraint)
    {
        auto constraint_ptr = std::unique_ptr<Sketcher::Constraint>(constraint->clone());
        return addConstraint(std::move(constraint_ptr));
    }
    int  SketcherObj::addConstraint(std::unique_ptr<Sketcher::Constraint> constraint)
    {
        if (!constraint) {
            return -1;
        }

        // Basic index validation: elements used by a constraint must exist.
        auto isValidGeoId = [this](int geoId) {
            return geoId < 0 || (geoId < static_cast<int>(mGeoList.size()));
            };
        if (!isValidGeoId(constraint->First) || !isValidGeoId(constraint->Second)
            || !isValidGeoId(constraint->Third)) {
            return -2;
        }

        for (int i = 0; i < mConstraintList.size(); i++) {
            if (
                mConstraintList[i]->Type == constraint->Type &&
                mConstraintList[i]->First == constraint->First &&
                mConstraintList[i]->FirstPos == constraint->FirstPos &&
                mConstraintList[i]->Second == constraint->Second &&
                mConstraintList[i]->SecondPos == constraint->SecondPos &&
                mConstraintList[i]->Third == constraint->Third &&
                mConstraintList[i]->ThirdPos == constraint->ThirdPos
                )
            {
                return -1;
            }
        }
        Sketcher::Constraint* constNew = constraint.release();
        mConstraintList.push_back(constNew);
        ++m_constraintRevision;
        return mConstraintList.size() - 1;
    }
    const Sketcher::Constraint* SketcherObj::getConstraint(int index) const
    {
        if (index < 0 || index >= static_cast<int>(mConstraintList.size())) {
            return nullptr;
        }
        return mConstraintList[index];
    }
    int SketcherObj::findConstraint(const Sketcher::Constraint* pattern) const
    {
        if (!pattern) {
            return -1;
        }
        for (int i = 0; i < static_cast<int>(mConstraintList.size()); ++i) {
            const Sketcher::Constraint* c = mConstraintList[i];
            if (c->Type == pattern->Type && c->First == pattern->First
                && c->FirstPos == pattern->FirstPos && c->Second == pattern->Second
                && c->SecondPos == pattern->SecondPos && c->Third == pattern->Third
                && c->ThirdPos == pattern->ThirdPos) {
                return i;
            }
        }
        return -1;
    }
    int SketcherObj::setDatum(int constrId, double datum)
    {
        if (constrId < 0 || constrId >= static_cast<int>(mConstraintList.size())) {
            return -1;
        }

        Sketcher::Constraint* c = mConstraintList[constrId];
        if (!c->isDimensional() && c->Type != Sketcher::ConstraintType::Tangent
            && c->Type != Sketcher::ConstraintType::Perpendicular) {
            return -1;
        }

        const double oldValue = c->getValue();
        c->setValue(datum);
        const int err = solve();
        if (err != 0) {
            c->setValue(oldValue);  // keep the sketch consistent with the old datum
        }
        return err;
    }
    void SketcherObj::addConstraint(Sketcher::ConstraintType constrType, int firstGeoId, Sketcher::PointPos firstPos, int secondGeoId, Sketcher::PointPos secondPos, int thirdGeoId, Sketcher::PointPos thirdPos)
    {
        auto newConstr = createConstraint(
            constrType, firstGeoId, firstPos, secondGeoId, secondPos, thirdGeoId, thirdPos);

        this->addConstraint(std::move(newConstr));
    }
    std::unique_ptr<Sketcher::Constraint> SketcherObj::createConstraint(Sketcher::ConstraintType constrType, int firstGeoId, Sketcher::PointPos firstPos, int secondGeoId, Sketcher::PointPos secondPos, int thirdGeoId, Sketcher::PointPos thirdPos)
    {
        auto newConstr = std::make_unique<Sketcher::Constraint>();

        newConstr->Type = constrType;
        newConstr->First = firstGeoId;
        newConstr->FirstPos = firstPos;
        newConstr->Second = secondGeoId;
        newConstr->SecondPos = secondPos;
        newConstr->Third = thirdGeoId;
        newConstr->ThirdPos = thirdPos;
        return newConstr;
    }
    void SketcherObj::updateGeoSegment(int id)
    {
        if (id < mGeoList.size()) {
            mGeoSegment[mGeoList[id].get()] = getCurveSegment(mGeoList[id].get());
        }
    }
    void SketcherObj::setConstruction(int geoId, bool construction)
    {
        // Construction is a property of the sketch's own geometry; an external
        // reference is never part of mGeoList.
        if (geoId < 0 || geoId >= static_cast<int>(mGeoList.size())) {
            return;
        }
        mGeoList[geoId]->setConstruction(construction);
        if (construction) {
            mConstructionGeoIds.insert(geoId);
        }
        else {
            mConstructionGeoIds.erase(geoId);
        }
    }
    void SketcherObj::setConstraintVisible(int constrId, bool visible)
    {
        if (constrId >= 0 && constrId < static_cast<int>(mConstraintList.size())) {
            mConstraintList[constrId]->isVisible = visible;
        }
    }
    bool SketcherObj::removeConstraint(int p_index)
    {
        if (p_index < 0 || p_index >= static_cast<int>(mConstraintList.size())) {
            return false;
        }
        Sketcher::Constraint* constraint = mConstraintList[p_index];
        // What is keyed by the constraint itself - the annotation layout of the
        // widget, and where a dimension's annotation was dropped - goes with it, or a
        // later constraint allocated at the same address would inherit it.
        m_annotationDrops.erase(constraint);
        delete constraint;
        mConstraintList.erase(mConstraintList.begin() + p_index);
        ++m_constraintRevision;
        // The other constraints hold the elements they name, not vector positions, so
        // nothing has to be renumbered - the solver only has to see the sketch as it is
        // now.
        solve();
        return true;
    }
    void SketcherObj::setGeometryVisible(int geoId, bool visible)
    {
        if (visible) {
            mHiddenGeoIds.erase(geoId);
        }
        else {
            mHiddenGeoIds.insert(geoId);
        }
    }

    void SketcherObj::setPlane(const SketcherPlane2D& plane)
    {
        mPlane = plane;
        // The plane is data, and so is what it means for the shape the sketch hands
        // out: the matrix that puts the sketch's 2D coordinates into the model comes
        // straight from it. It has to be updated here, not by whoever draws the
        // sketch - a document that is only read (no widget exists yet) still has to
        // come out in the right place.
        planeTransform = updateTransform();
        // Pointing the camera at the plane is the widget's business: it sees the change
        // through the revision instead (SketcherObjWidget::syncWithSketch).
        ++m_planeRevision;
    }
    SketcherObj::CurveSegment SketcherObj::getCurveSegment(Part::Geometry* geo)
    {
        CurveSegment seg;
        CurveConvert::toVector2D(geo, 50, seg.point, seg.params);
        if (geo->isDerivedFrom<Part::GeomCurve>()) {
            if (geo->is<Part::GeomArcOfCircle>()) {
                Part::GeomArcOfCircle* curve = static_cast<Part::GeomArcOfCircle*>(geo);
                seg.sepoints.push_back({ curve->getStartPoint(),PointPos::start });
                seg.sepoints.push_back({ curve->getEndPoint() ,PointPos::end });
                seg.sepoints.push_back({ curve->getCenter() ,PointPos::mid });
            }
            else if (geo->is<Part::GeomLineSegment>()) {
                Part::GeomLineSegment* lineSeg = static_cast<Part::GeomLineSegment*>(geo);
                seg.sepoints.push_back({ lineSeg->getStartPoint(),PointPos::start });
                seg.sepoints.push_back({ lineSeg->getEndPoint(),PointPos::end });
            }
            else if (geo->is<Part::GeomArcOfConic>()) {
                Part::GeomArcOfConic* curve = static_cast<Part::GeomArcOfConic*>(geo);
                seg.sepoints.push_back({ curve->getStartPoint(),PointPos::start });
                seg.sepoints.push_back({ curve->getEndPoint() ,PointPos::end });
                seg.sepoints.push_back({ curve->getCenter() ,PointPos::mid });
            }
            else if (geo->is<Part::GeomCircle>()) {
                Part::GeomCircle* curve = static_cast<Part::GeomCircle*>(geo);
                seg.sepoints.push_back({ curve->getCenter() ,PointPos::mid });
            }
            else if (geo->isDerivedFrom<Part::GeomConic>()) {
                // A full conic (ellipse, hyperbola, parabola) has no ends, so its centre
                // is the only anchor it can offer. Without this an ellipse - which is
                // what a circle tilted against the sketch plane projects to - had no
                // marker at all: nothing to draw, to pick or to snap to.
                const auto* conic = static_cast<const Part::GeomConic*>(geo);
                seg.sepoints.push_back({ conic->getCenter() ,PointPos::mid });
            }
            else if (geo->is<Part::GeomBSplineCurve>()) {
                Part::GeomBSplineCurve* curve = static_cast<Part::GeomBSplineCurve*>(geo);
                std::vector<Base::Vector3d>poles = curve->getPoles();
                for (int i = 0; i < poles.size(); i++) {
                    seg.sepoints.push_back({ poles[i],PointPos::mid });
                }
            }
        }
        else if (geo->is<Part::GeomPoint>()) {
            Base::Vector3d pos = static_cast<Part::GeomPoint*>(geo)->getPoint();
            seg.sepoints.push_back({ pos, PointPos::start });
        }
        return seg;
    }
    SketcherObj::CurveSegment& SketcherObj::segmentOf(Part::Geometry* geo)
    {
        // An entry existing means "this geometry is sampled": a point samples to no
        // polyline at all, so the content cannot be used to tell.
        const auto found = mGeoSegment.find(geo);
        if (found != mGeoSegment.end()) {
            return found->second;
        }
        return mGeoSegment.emplace(geo, getCurveSegment(geo)).first->second;
    }
    const SketcherObj::CurveSegment* SketcherObj::findSegment(
        const Part::Geometry* geo) const
    {
        const auto found = mGeoSegment.find(const_cast<Part::Geometry*>(geo));
        if (found == mGeoSegment.end()) {
            return nullptr;
        }
        return &found->second;
    }
    SketcherPlane2D SketcherObj::getPlane()
    {
        return mPlane;
    }    
    void SketcherObj::getPlaneNormal(double* p)
    {
        p[0] = mPlane.normal.x;
        p[1] = mPlane.normal.y;
        p[2] = mPlane.normal.z;
    }
    bool SketcherObj::getGeometryPointSketch(int geoId, PointPos pos, Base::Vector2d& out) const
    {
        // External references are drawn and labelled like the sketch's own curves, so
        // the lookup has to cover both halves of the solver list.
        const Part::Geometry* geo = resolveGeometry(geoId);
        // The origin is the fallback for the root-point id, but only when that id is
        // not one of the external curves (the ids of the block run up to -1).
        if (geo == nullptr && geoId == Sketcher::GeoEnum::RtPnt
            && (pos == PointPos::start || pos == PointPos::mid)) {
            out.x = 0.0;
            out.y = 0.0;
            return true;
        }
        if (!geo) {
            return false;
        }
        if (geo->is<Part::GeomPoint>()) {
            const Base::Vector3d p = static_cast<const Part::GeomPoint*>(geo)->getPoint();
            out.x = p.x;
            out.y = p.y;
            return true;
        }
        auto it = mGeoSegment.find(const_cast<Part::Geometry*>(geo));
        if (it == mGeoSegment.end()) {
            return false;
        }
        for (const auto& sp : it->second.sepoints) {
            if (sp.pointPos == pos) {
                out.x = sp.coord.x;
                out.y = sp.coord.y;
                return true;
            }
        }
        return false;
    }
    bool SketcherObj::getGeometryCenterSketch(int geoId, Base::Vector2d& out) const
    {
        if (geoId == NoGeoId) {
            return false;
        }
        const Part::Geometry* geo = resolveGeometry(geoId);
        // The origin stands in for the root-point id, but only when that id does not
        // name one of the external curves.
        if (geo == nullptr && geoId == Sketcher::GeoEnum::RtPnt) {
            out.x = 0.0;
            out.y = 0.0;
            return true;
        }
        if (!geo) {
            return false;
        }
        Base::Vector3d center;
        // An arc's centre is the centre of the circle it is part of. The bounded-curve
        // branch further down would answer with the middle of its chord instead, which
        // is not a centre at all - and it is the branch that used to win for arcs,
        // because the abstract GeomArcOfConic test does not answer for them. Their
        // concrete types are asked for instead.
        const bool isArc = geo->is<Part::GeomArcOfCircle>()
            || geo->is<Part::GeomArcOfEllipse>()
            || geo->is<Part::GeomArcOfHyperbola>()
            || geo->is<Part::GeomArcOfParabola>();
        if (isArc) {
            center = static_cast<const Part::GeomArcOfConic*>(geo)->getCenter();
        }
        else if (geo->isDerivedFrom<Part::GeomConic>()) {
            center = static_cast<const Part::GeomConic*>(geo)->getCenter();
        }
        else if (geo->is<Part::GeomPoint>()) {
            center = static_cast<const Part::GeomPoint*>(geo)->getPoint();
        }
        else if (geo->isDerivedFrom<Part::GeomBoundedCurve>()) {
            const auto* bounded = static_cast<const Part::GeomBoundedCurve*>(geo);
            center = (bounded->getStartPoint() + bounded->getEndPoint()) * 0.5;
        }
        else if (geo->isDerivedFrom<Part::GeomCurve>()) {
            const auto it = mGeoSegment.find(const_cast<Part::Geometry*>(geo));
            if (it != mGeoSegment.end() && !it->second.point.empty()) {
                center = it->second.point[it->second.point.size() / 2];
            }
            else {
                return false;
            }
        }
        else {
            return false;
        }
        out.x = center.x;
        out.y = center.y;
        return true;
    }
    SketcherObj::ConstraintStatus SketcherObj::getConstraintStatus(int p_constrId) const
    {
        const auto contains = [p_constrId](const std::vector<int>& list) {
            return std::find(list.begin(), list.end(), p_constrId) != list.end();
            };
        // A constraint can show up in more than one list; the most severe one is the
        // one worth showing.
        if (contains(lastConflicting)) {
            return ConstraintStatus::Conflicting;
        }
        if (contains(lastMalformedConstraints)) {
            return ConstraintStatus::Malformed;
        }
        if (contains(lastRedundant)) {
            return ConstraintStatus::Redundant;
        }
        if (contains(lastPartiallyRedundant)) {
            return ConstraintStatus::PartiallyRedundant;
        }
        return ConstraintStatus::Ok;
    }
}
