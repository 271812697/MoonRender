#include "Sketcher/SketcherObj.h"
#include "editor/Toolbar/sketchToolbar.h"
#include "Geometry.h"
#include "renderer/SceneView.h"

#include "Core/Global/ServiceLocator.h"
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
#include <TopTools_IndexedMapOfShape.hxx>
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
		doneFaceShape = doneWireShape.makeElementFace(nullptr, "Part::FaceMakerBullseye");
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
        lastDoF=solvedSketch.setUpSketch(
            GeoList, mConstraintList,0);
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
