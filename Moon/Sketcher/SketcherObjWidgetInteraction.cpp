#include "Sketcher/SketcherObjWidget.h"
#include "renderer/SceneView.h"
#include "editor/Toolbar/sketchToolbar.h"
#include "Core/Global/ServiceLocator.h"
#include <cmath>
namespace MOON {
    /** The widget is created when a sketch is opened for editing, and dies with it.
     * It does not own the sketch: the feature does, and the sketch outlives it (a
     * document can be solved and written without any widget at all). */
    SketcherObjWidget::SketcherObjWidget(SketcherObj* p_sketch)
        : EventWidget("SketcherObj"), m_sketch(p_sketch)
    {
        setActive(true);
    }
    SketcherObjWidget::~SketcherObjWidget()
    {
    }
    static double pointToSegmentDist(const Base::Vector3d& p, const Base::Vector3d& s, const Base::Vector3d& e, double& u) {
        Base::Vector3d se = e - s;
        Base::Vector3d sp = p - s;
        double t = sp.Dot(se) / se.Dot(se);
        if (t < 0.0) {
            u = 0.0;
            return sp.Length();
        }

        if (t > 1.0) {
            u = 1.0;
            return (p - e).Length();
        }
        u = t;
        Base::Vector3d proj = s + t * se;
        return (p - proj).Length();
    };
    void SketcherObjWidget::onMouseMove()
    {
        if (!isHaveActiveHandler && isInEdit) {
            updateConstraintLabelInteraction();
            // While the cursor rests on a dimension label (or drags one) the
            // mouse must not select/move the geometry underneath it.
            if (m_labelDrag >= 0 || m_labelHover != -1) {
                preSelectGeoId = { NoGeoId, PointPos::none };
                return;
            }
        }
        onSketchPosP2 = onSketchPosMove;
        Base::Vector2d preOnSketchPosMove = onSketchPosMove;
        if (!isHaveActiveHandler && isInEdit) {
            pickGeo();
            if (selectState == Stop && preSelectGeoId.GeoId != NoGeoId) {
                selectState = Hot;
            }
            else if (selectState == OperationGeo) {
                const bool singleCircleArcDrag = [&]() {
                    if (selectIds.size() != 1) {
                        return false;
                    }
                    Part::Geometry* geo = m_sketch->getGeometry(selectIds[0].GeoId);
                    return geo
                        && (geo->is<Part::GeomCircle>() || geo->is<Part::GeomArcOfCircle>());
                }();
                // Rim drags resize a circle/arc. They go through the solver
                // with an absolute target: the grabbed rim follows the mouse
                // while the center is pinned by a temporary coincidence, so
                // constrained composites (e.g. a slot cap) resize without the
                // center drifting.
                const bool radiusDrag
                    = singleCircleArcDrag && selectIds[0].pointPos == PointPos::none;

                if (singleCircleArcDrag && !radiusDrag) {
                    // Endpoint/center drags keep the element's own parameter
                    // semantics: an endpoint slides on the unchanged circle and
                    // the center follows the mouse. moveGeo() implements those
                    // directly; the solver's relative translation would move
                    // the whole element together with the grabbed endpoint.
                    if (!m_dragSolverInit) {
                        // Normalize curves added since the last m_sketch->solve() so the
                        // range angles read by moveGeo() are canonical.
                        m_sketch->solve();
                        m_dragSolverInit = true;
                    }
                    for (const auto& sel : selectIds) {
                        moveGeo(
                            sel,
                            static_cast<float>(onSketchPosMove.x - preOnSketchPosMove.x),
                            static_cast<float>(onSketchPosMove.y - preOnSketchPosMove.y)
                        );
                    }
                    m_sketch->solve();
                    return;
                }

                std::vector<Sketcher::GeoElementId> dragIds;
                dragIds.reserve(selectIds.size());
                for (const auto& sel : selectIds) {
                    // An external reference cannot be dragged: the solver is told it is
                    // fixed, so moving it would only fight with its own definition.
                    if (sel.GeoId < 0) {
                        continue;
                    }
                    dragIds.emplace_back(sel.GeoId, sel.pointPos);
                }
                if (dragIds.empty()) {
                    selectState = Stop;
                    return;
                }
                if (!m_dragSolverInit) {
                    // Rebuild the solver state from the current geometry list before anchoring
                    // the drag. Curves committed after the last solve would otherwise be missing
                    // from the solver and would disappear when the drag hands the solved geometry
                    // back.
                    m_sketch->solve();
                    m_dragSolverInit = m_sketch->beginMove(dragIds);
                }
                if (m_dragSolverInit) {
                    Base::Vector3d moveTo;
                    bool relative = true;
                    if (radiusDrag) {
                        moveTo = Base::Vector3d(onSketchPosMove.x, onSketchPosMove.y, 0.0);
                        relative = false;  // rim follows the mouse, center stays
                    }
                    else {
                        // Relative displacement moves the grabbed elements
                        // rigidly (group/line drags).
                        const Base::Vector2d totalDelta = onSketchPosMove - onSketchPosP1;
                        moveTo = Base::Vector3d(totalDelta.x, totalDelta.y, 0.0);
                    }
                    const int status = m_sketch->moveGeometries(dragIds, moveTo, relative);
                    if (status == 0) {
                        m_sketch->takeSolvedGeometry();
                    }
                    return;  // solver path already handled this frame
                }

                bool solveS = false;
                for (int i = 0; i < selectIds.size(); i++) {
                    moveGeo(
                        selectIds[i],
                        onSketchPosMove.x - preOnSketchPosMove.x,
                        onSketchPosMove.y - preOnSketchPosMove.y
                    );
                    solveS = true;
                }
                if (solveS) {
                    m_sketch->solve();
                }
            }
            else if (selectState == Hot && preSelectGeoId.GeoId == NoGeoId) {
                selectState = Stop;
            }
        }
    }

    void SketcherObjWidget::onLeftMousePressed()
    {
        if (!isHaveActiveHandler && isInEdit) {
            auto [mx, my] = m_sceneView->getInutState().GetMousePosition();
            // The annotation itself is a handle as well: a length dimension's line -
            // arrows included - moves the dimension, an angle's arc pulls the annotation
            // in and out. The caption stays its own handle and only slides along them.
            int annotationHit = -1;
            LabelHandle annotationHandle = LabelHandle::Caption;
            pickLabelTarget(
                static_cast<float>(mx),
                static_cast<float>(my),
                annotationHit,
                annotationHandle
            );
            if (annotationHit >= 0 && annotationHandle != LabelHandle::Caption) {
                m_labelDrag = annotationHit;
                m_labelDragHandle = annotationHandle;
                m_lastLabelClick = -1;
                clearSelect();
                selectState = Stop;
                preSelectGeoId = { NoGeoId, PointPos::none };
                return;
            }
            const int labelHit = annotationHit;
            if (labelHit >= 0) {
                m_labelDragHandle = LabelHandle::Caption;
                // Where the caption is carried to is read off the cursor as it moves (a
                // dimension's line, an angle's arc and a radius' leader each in their
                // own way - see updateConstraintLabelInteraction), so the press itself
                // only has to start the drag.
                m_labelDrag = labelHit;
                clearSelect();
                selectState = Stop;
                preSelectGeoId = { NoGeoId, PointPos::none };

                const auto now = std::chrono::steady_clock::now();
                const bool isDoubleClick = m_lastLabelClick == labelHit
                    && std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastLabelClickTime).count()
                    < 400;
                if (isDoubleClick) {
                    m_lastLabelClick = -1;
                    m_labelDrag = -1;
                    editConstraintValue(labelHit);
                }
                else {
                    m_lastLabelClick = labelHit;
                    m_lastLabelClickTime = now;
                }
                return;
            }
            m_lastLabelClick = -1;
        }
        sketchDrawRect = true;
        onSketchPosP1 = getMouseHitSketchPlanePoint();
        onSketchPosClicked = onSketchPosP1;
        m_dragSolverInit = false;
        if (preSelectGeoId.GeoId == NoGeoId) {
            pickGeo();
        }
      
        if (!isHaveActiveHandler) {
            if (selectState == Hot) {
                if (preSelectGeoId.GeoId != NoGeoId) {
                    const bool alreadySelected = [this]() {
                        for (const auto& sel : selectIds) {
                            if (sel.GeoId == preSelectGeoId.GeoId
                                && sel.pointPos == preSelectGeoId.pointPos) {
                                return true;
                            }
                        }
                        return false;
                    }();
                    if (alreadySelected && preSelectGeoId.pointPos != PointPos::none) {
                        // Several points coincide; clicking the already
                        // selected one cycles to the next unselected point.
                        SelectGeoId next;
                        if (findNextCoincidentPoint(onSketchPosClicked, preSelectGeoId, next)) {
                            if (selectMode == OverrideSelect) {
                                clearSelect();
                            }
                            addSelect(next);
                            preSelectGeoId = next;
                            selectState = OperationGeo;
                            return;
                        }
                    }
                    if (selectMode == OverrideSelect) {
                        clearSelect();
                    }
                    addSelect(preSelectGeoId);
                    selectState = OperationGeo;
                }
                else
                {
                    selectState = Stop;
                }
            }
            else if (selectState == Stop) {
                selectState = DragRect;
            }
        }
    }
    void SketcherObjWidget::onLeftMouseReleased()
    {
        if (m_labelDrag >= 0) {
            // A dimension label was being dragged; the label position is kept
            // in m_labelManualOffsetSketch, so simply end the drag here.
            m_labelDrag = -1;
            return;
        }
        sketchDrawRect = false;
        onSketchPosP2 = getMouseHitSketchPlanePoint();
        if (!isHaveActiveHandler) {
            if (selectState == OperationGeo) {
                selectState = Hot;
                m_dragSolverInit = false;
                m_sketch->resetInitialMove();
            }
            else if (selectState == DragRect) {
                if (selectMode == OverrideSelect) {
                    clearSelect();
                }
                Base::Vector2d minPt(std::min(onSketchPosP1.x, onSketchPosP2.x), std::min(onSketchPosP1.y, onSketchPosP2.y));
                Base::Vector2d maxPt(std::max(onSketchPosP1.x, onSketchPosP2.x), std::max(onSketchPosP1.y, onSketchPosP2.y));
                std::vector<Base::Vector2d> selectedPointCoords;
                const auto alreadyPicked = [&selectedPointCoords](const Base::Vector3d& c) {
                    for (const auto& p : selectedPointCoords) {
                        const double dx = p.x - c.x;
                        const double dy = p.y - c.y;
                        if (dx * dx + dy * dy < 1.0e-8) {
                            return true;
                        }
                    }
                    return false;
                };
			    for (int i = 0;i < m_sketch->geometries().size();i++) {
                    if (!m_sketch->isGeometryVisible(i)) {
                        continue;  // hidden geometry is not selectable
                    }
                    if (m_sketch->isConstructionGeometry(i)) {
                        continue;  // construction aids are not user selectable
                    }
				    auto& seg = m_sketch->segmentOf(m_sketch->geometries()[i].get());
                    bool isInside = true;
                    for (int j = 0;j < seg.point.size();j++) {
                        bool flag = seg.point[j].x >= minPt.x && seg.point[j].x <= maxPt.x
                            && seg.point[j].y >= minPt.y && seg.point[j].y <= maxPt.y;
                        if (!flag) {
                            isInside = false;
                            break;
                        }
                    }
                    if (isInside) {
                        addSelect({ i,PointPos::none });
                    }
                    else
                    {
                        for (int j = 0; j < seg.sepoints.size(); j++) {
                            bool flag = seg.sepoints[j].coord.x >= minPt.x && seg.sepoints[j].coord.x <= maxPt.x
                                && seg.sepoints[j].coord.y >= minPt.y && seg.sepoints[j].coord.y <= maxPt.y;
                            if (flag && !alreadyPicked(seg.sepoints[j].coord)) {
                                addSelect({ i,seg.sepoints[j].pointPos });
                                selectedPointCoords.push_back(
                                    Base::Vector2d(seg.sepoints[j].coord.x, seg.sepoints[j].coord.y)
                                );
                                break;
                            }
                        }
                    }
                }
                // The origin belongs to the axes and not to the sketch's geometry, so
                // the loop above never reaches it - but it is a pick target of its own,
                // and the point a sketch is most often constrained to, so a rubber band
                // over it has to take it as well. A point already picked at the same
                // place wins: the origin and a curve endpoint sitting on it are one
                // position, and taking both would only leave an extra entry behind.
                const Base::Vector3d origin(0.0, 0.0, 0.0);
                if (origin.x >= minPt.x && origin.x <= maxPt.x
                    && origin.y >= minPt.y && origin.y <= maxPt.y
                    && !alreadyPicked(origin)) {
                    addSelect({ Sketcher::GeoEnum::HAxis, PointPos::start });
                }
                selectState = Stop;
            }
        }
    }
    void SketcherObjWidget::onKeyPress(const std::string& key)
    {
        if (key == "DELETE" && !isHaveActiveHandler) {
            std::vector<int>deletList(selectIds.size());
            for (int i = 0; i < selectIds.size(); i++) {
                deletList[i] = selectIds[i].GeoId;
            }
            m_sketch->deleteGeometries(deletList);
            selectIds.clear();
            m_sketch->solve();
        }
        else if (key == "CONTROL_L") {
            selectMode = AppendSelect;
        }
    }
    void SketcherObjWidget::onKeyRelease(const std::string& key)
    {
        if (key == "CONTROL_L") {
            selectMode = OverrideSelect;
        }
    }
    int SketcherObjWidget::getPickGeoIndex(const Base::Vector2d& pos, const Base::Matrix4D& mat)
    {

        Base::Matrix4D trans = mat * m_sketch->getplaneTransform();
        Base::Vector3d p1 = trans * Base::Vector3d(pos.x, pos.y, 0);

        int ret = -1;
        double deltaTole = 15.0;
        double minDist = 10000.0;
        // 遍历所有几何图元
        for (int i = 0; i < m_sketch->geometries().size(); i++) {
            if (!m_sketch->isGeometryVisible(i)) {
                continue;  // hidden geometry is not pickable
            }
            Part::Geometry* geo = m_sketch->geometries()[i].get();
            if (geo->isDerivedFrom<Part::GeomCurve>()) {
                auto& segment = m_sketch->segmentOf(geo);
                int segCount = segment.point.size();
                if (segCount < 2)
                    continue;
                // 遍历每一段线段 [k] → [k+1]
                for (int k = 0; k < segCount - 1; k++) {
                    Base::Vector3d s = segment.point[k];
                    Base::Vector3d e = segment.point[k + 1];
                    // ✅ 使用 Lambda 计算真正的点到线段距离
                    double u;
                    double dist = pointToSegmentDist(p1, trans * s, trans * e, u);

                    if (dist < deltaTole && dist < minDist) {
                        minDist = dist;
                        ret = i;
                    }
                }
            }
        }
        return ret;
    }
    SketcherObj::SelectGeoId SketcherObjWidget::testSelect(const Base::Vector2d& pos)
    {
        Maths::FMatrix4 mat = m_sceneView->GetCamera()->GetViewPortMatrix();
        Base::Matrix4D viewPortMat(
            mat.data[0], mat.data[1], mat.data[2], mat.data[3],
            mat.data[4], mat.data[5], mat.data[6], mat.data[7],
            mat.data[8], mat.data[9], mat.data[10], mat.data[11],
            mat.data[12], mat.data[13], mat.data[14], mat.data[15]
        );
        Base::Matrix4D trans = viewPortMat * m_sketch->getplaneTransform();
        Base::Vector3d p1 = trans * Base::Vector3d{ pos.x,pos.y,0.0 };
       double deltaTole = 5.0;
       double minDist = 10000.0;
       SelectGeoId ret = { NoGeoId,PointPos::none };
       // A hit has to be tracked separately: the first external curve carries the id
       // -1, so the id cannot double as "nothing was hit".
       bool hit = false;

        // The sketch origin is a pick target of its own and it is tested first: it is
        // where both axes start (the root point - GeoEnum gives it the id of the
        // horizontal axis, the point position start), and it is what a sketch is most
        // often constrained to. Left to the loops below, the axis lines - which pass
        // through it - or a curve endpoint sitting on it would take the click. A
        // coincident element can still be reached by clicking again: the selection
        // cycles through the points that lie on top of each other.
        const double originTole = 10.0;
        if ((p1 - trans * Base::Vector3d(0.0, 0.0, 0.0)).Length() < originTole) {
            ret.GeoId = Sketcher::GeoEnum::HAxis;
            ret.pointPos = PointPos::start;
            return ret;
        }

        // The candidates are the sketch's own curves plus the external ones (the axes
        // and the origin among them): an external curve is a reference the user may
        // constrain to, so it has to be selectable. This used to be skipped while a
        // draw handler was active, which is exactly when a reference is needed most -
        // the smart dimension picks the point it measures against through here, and
        // "constrain this to the origin" is one of those picks. The drawing tools do
        // not come through this function at all: the sketch only refreshes its hover
        // pick while no handler runs (see onMouseMove), so nothing is picked here for
        // them.
        std::vector<std::pair<int, Part::Geometry*>> candidates;
        candidates.reserve(m_sketch->geometries().size() + m_sketch->getExternalCurveCount());
        for (int i = 0; i < static_cast<int>(m_sketch->geometries().size()); ++i) {
            if (!m_sketch->isGeometryVisible(i)) {
                continue;
            }
            candidates.emplace_back(i, m_sketch->geometries()[i].get());
        }
        for (int i = 0; i < m_sketch->getExternalCurveCount(); ++i) {
            Part::Geometry* geo = const_cast<Part::Geometry*>(
                m_sketch->getExternalCurve(m_sketch->getExternalGeoId(i)));
            // Only a curve that has been sampled can be hit; a missing cache entry
            // must not be turned into an empty one here.
            if (geo != nullptr && m_sketch->findSegment(geo) != nullptr) {
                candidates.emplace_back(m_sketch->getExternalGeoId(i), geo);
            }
        }

        // The points of every curve first: an endpoint or a centre is what a constraint
        // usually wants, and it is a smaller target than the curve it belongs to.
        for (const auto& [geoId, geo] : candidates) {
            if (m_sketch->isAxisCurve(geoId)) {
                // The axes are not picked by their points: the only one that is a
                // feature of its own is the root point, and the origin test above has
                // already taken it. Their defining segment reaches from the origin to a
                // far end that means nothing, and offering that end as a pick would
                // hand the user a point that is not drawn anywhere.
                continue;
            }
            auto& segment = m_sketch->segmentOf(geo);
            for (int j = 0; j < segment.sepoints.size(); j++) {
                double dist = (p1 - trans * segment.sepoints[j].coord).Length();
                if (dist < deltaTole && dist < minDist) {
                    minDist = dist;
                    ret.GeoId = geoId;
                    ret.pointPos = segment.sepoints[j].pointPos;
                    hit = true;
                }
            }
        }
        // Endpoints and centers win over the curves themselves; the curves are only
        // tested when no point was close enough.
        if (!hit) {
            for (const auto& [geoId, geo] : candidates) {
                if (m_sketch->isAxisCurve(geoId)) {
                    // The axes are tried after every curve, see below.
                    continue;
                }
                auto& segment = m_sketch->segmentOf(geo);
                if (geo->isDerivedFrom<Part::GeomCurve>()) {
                    for (int j = 0; j < segment.point.size() - 1; j++) {
                        double u = 0.0;
                        double dist = pointToSegmentDist(
                            p1,
                            trans * segment.point[j],
                            trans * segment.point[j + 1],
                            u);

                        if (dist < deltaTole && dist < minDist) {
                            minDist = dist;
                            ret.GeoId = geoId;
                        }
                    }
                }
                else if (geo->is<Part::GeomPoint>())
                {
                    Base::Vector3d pp = static_cast<Part::GeomPoint*>(geo)->getPoint();
                    double dist = (p1 - trans * pp).Length();
                    if (dist < deltaTole && dist < minDist) {
                        minDist = dist;
                        ret.GeoId = geoId;
                    }
                }
            }
        }
        // Last of all, and only when nothing else was close enough: the axes, hit as
        // the infinite lines they stand for - their geometry only reaches from the
        // origin outwards (it has to start there, that is the root point) while the
        // user clicks anywhere along the line that is drawn for them.
        //
        // They come last because they are infinite: a click anywhere along one of them
        // is "on the line", so an axis would take every pick of a curve of the sketch
        // that happens to lie on it - a line drawn over the y axis could never be
        // selected again. FreeCAD picks in that order as well (its geometry is tested
        // before the axis cross).
        if (!hit) {
            for (const auto& [geoId, geo] : candidates) {
                if (!m_sketch->isAxisCurve(geoId)) {
                    continue;
                }
                const Base::Vector3d origin = trans * Base::Vector3d(0.0, 0.0, 0.0);
                const Base::Vector3d tip = trans * (geoId == Sketcher::GeoEnum::VAxis
                    ? Base::Vector3d(0.0, 1.0, 0.0)
                    : Base::Vector3d(1.0, 0.0, 0.0));
                const Base::Vector3d dir = tip - origin;
                const double len = dir.Length();
                if (len > 1e-12) {
                    const double dist = (p1 - origin).Cross(dir).Length() / len;
                    if (dist < deltaTole && dist < minDist) {
                        minDist = dist;
                        ret.GeoId = geoId;
                        ret.pointPos = PointPos::none;
                        hit = true;
                    }
                }
            }
        }
        return ret;
    }
    bool SketcherObjWidget::findNextCoincidentPoint(
        const Base::Vector2d& pos,
        const SelectGeoId& current,
        SelectGeoId& next
    ) const
    {
        const Maths::FMatrix4 mat = m_sceneView->GetCamera()->GetViewPortMatrix();
        const Base::Matrix4D viewPortMat(
            mat.data[0], mat.data[1], mat.data[2], mat.data[3],
            mat.data[4], mat.data[5], mat.data[6], mat.data[7],
            mat.data[8], mat.data[9], mat.data[10], mat.data[11],
            mat.data[12], mat.data[13], mat.data[14], mat.data[15]
        );
        const Base::Matrix4D trans = viewPortMat * m_sketch->getplaneTransform();
        const Base::Vector3d p1 = trans * Base::Vector3d(pos.x, pos.y, 0.0);
        constexpr double kTol = 5.0;

        const auto isSelected = [this](const SelectGeoId& s) {
            for (const auto& sel : selectIds) {
                if (sel.GeoId == s.GeoId && sel.pointPos == s.pointPos) {
                    return true;
                }
            }
            return false;
        };

        for (int i = 0; i < static_cast<int>(m_sketch->geometries().size()); ++i) {
            if (!m_sketch->isGeometryVisible(i)) {
                continue;
            }
            const SketcherObj::CurveSegment* segment = m_sketch->findSegment(m_sketch->geometries()[i].get());
            if (segment == nullptr) {
                continue;
            }
            const auto& sePoints = segment->sepoints;
            for (const auto& sp : sePoints) {
                if (sp.pointPos == PointPos::none) {
                    continue;
                }
                const double dist = (p1 - trans * sp.coord).Length();
                if (dist < kTol) {
                    const SelectGeoId cand{ i, sp.pointPos };
                    if (!(cand.GeoId == current.GeoId && cand.pointPos == current.pointPos)
                        && !isSelected(cand)) {
                        next = cand;
                        return true;
                    }
                }
            }
        }
        return false;
    }
    std::vector<int> SketcherObjWidget::getSelectIds() const
    {
        std::vector<int>selectIdLists(selectIds.size());
        for (int i = 0; i < selectIds.size(); i++) {
            selectIdLists[i] = selectIds[i].GeoId;
        }
        return selectIdLists;
    }

    void SketcherObjWidget::addSelect(int id)
    {
        addSelect({ id,PointPos::none });
    }

    void SketcherObjWidget::removeSelect(const std::vector<int>& idList)
    {
        int left = 0;
        for (int right = 0; right < selectIds.size();right++) {
            bool removeFlag = false;
            for (int i = 0; i < idList.size(); i++) {
                if (selectIds[right].GeoId == idList[i]) {
                    removeFlag = true;
                    break;
                }
            }
            if (!removeFlag) {
                selectIds[left++] = selectIds[right];
            }
        }
        selectIds.resize(left);
    }
    bool SketcherObjWidget::snapPoint(Base::Vector2d& pos, const std::set<int>& avoid)
    {
        Maths::FMatrix4 mat = m_sceneView->GetCamera()->GetViewPortMatrix();
        Base::Matrix4D pla(
            mat.data[0], mat.data[1], mat.data[2], mat.data[3],
            mat.data[4], mat.data[5], mat.data[6], mat.data[7],
            mat.data[8], mat.data[9], mat.data[10], mat.data[11],
            mat.data[12], mat.data[13], mat.data[14], mat.data[15]
        );
        Base::Matrix4D trans = pla * m_sketch->getplaneTransform();
        //get the screen pos
        Base::Vector3d screenpPos = trans * Base::Vector3d{ pos.x,pos.y,0.0 };
        double deltaTole = 10.0;
        double minDist = 10000.0;
        bool ret = false;
        // Projected external curves are snap targets as well: they are the references
        // the sketch is drawn against, and a new point usually wants to land on one of
        // them. They are tested after the sketch's own geometry at the same stage, so
        // an internal target within range wins.
        const auto snapToExternalPoints = [&]() {
            for (int i = 0; i < m_sketch->getExternalCurveCount(); ++i) {
                const int geoId = m_sketch->getExternalGeoId(i);
                if (avoid.count(geoId)) {
                    continue;
                }
                Part::Geometry* geo = const_cast<Part::Geometry*>(m_sketch->getExternalCurve(geoId));
                if (geo == nullptr) {
                    continue;
                }
                const CurveSegment* segment = m_sketch->findSegment(geo);
                if (segment == nullptr) {
                    continue;  // not sampled: nothing to snap to yet
                }
                for (const SegPoint& segPoint : segment->sepoints) {
                    const double dist = (screenpPos - trans * segPoint.coord).Length();
                    if (dist < deltaTole && dist < minDist) {
                        minDist = dist;
                        ret = true;
                        pos = { segPoint.coord.x, segPoint.coord.y };
                    }
                }
            }
        };
        // travel all segments
        for (int i = 0; i < m_sketch->geometries().size(); i++) {
            if (!avoid.count(i)) {
                if (m_sketch->isConstructionGeometry(i)) {
                    continue;  // construction aids are not snap targets
                }
                if (!m_sketch->isGeometryVisible(i)) {
                    continue;  // hidden geometry is not a snap target
                }
                Part::Geometry* geo = m_sketch->geometries()[i].get();
                auto& segment = m_sketch->segmentOf(geo);
                for (int j = 0;j < segment.sepoints.size();j++) {
                    double dist = (screenpPos - trans * segment.sepoints[j].coord).Length();
                    if (dist < deltaTole && dist < minDist) {
                        minDist = dist;
                        ret = true;
                        pos = { segment.sepoints[j].coord.x, segment.sepoints[j].coord.y };
                    }
                }
            }

        }
        snapToExternalPoints();
        if (!ret) {
            //snap to orgin or XAxis or YAxis
            Base::Vector3d screenOrigin = trans * Base::Vector3d(0, 0, 0);
            double dist = (screenpPos - screenOrigin).Length();
            if (dist < deltaTole) {
                pos = { 0.0, 0.0 };
                return true;
            }
            double deltaX = abs(screenpPos.x - screenOrigin.x);
            double deltaY = abs(screenpPos.y - screenOrigin.y);
            if (deltaX < deltaTole && deltaX < deltaY) {
                pos.x = 0.0;
                return true;
            }
            if (deltaY < deltaTole && deltaY < deltaX) {
                pos.y = 0.0;
                return true;
            }
            //snap to curve
            for (int i = 0; i < m_sketch->geometries().size(); i++) {
                if (!avoid.count(i)) {
                    if (m_sketch->isConstructionGeometry(i)) {
                        continue;
                    }
                    if (!m_sketch->isGeometryVisible(i)) {
                        continue;
                    }
                    Part::Geometry* geo = m_sketch->geometries()[i].get();
                    auto& segment = m_sketch->segmentOf(geo);
                    if (geo->isDerivedFrom<Part::GeomCurve>()) {
                        for (int j = 0;j < segment.point.size() - 1;j++) {
                            double u = 0.0;
                            double dist = pointToSegmentDist(
                                screenpPos,
                                trans * segment.point[j],
                                trans * segment.point[j + 1],
                                u);
                            if (dist < deltaTole && dist < minDist) {
                                minDist = dist;
                                ret = true;
                                u = segment.params[j] + u * (segment.params[j + 1] - segment.params[j]);
                                Base::Vector3d pp = static_cast<Part::GeomCurve*>(geo)->value(u);
                                pos = { pp.x, pp.y };
                            }
                        }
                    }
                }
            }
            // Same for the curves themselves, again after the sketch's own.
            for (int i = 0; i < m_sketch->getExternalCurveCount(); ++i) {
                const int geoId = m_sketch->getExternalGeoId(i);
                if (avoid.count(geoId)) {
                    continue;
                }
                Part::Geometry* geo = const_cast<Part::Geometry*>(m_sketch->getExternalCurve(geoId));
                if (geo == nullptr || !geo->isDerivedFrom<Part::GeomCurve>()) {
                    continue;
                }
                const CurveSegment* segment = m_sketch->findSegment(geo);
                if (segment == nullptr) {
                    continue;
                }
                for (int j = 0; j + 1 < static_cast<int>(segment->point.size()); j++) {
                    double u = 0.0;
                    const double dist = pointToSegmentDist(
                        screenpPos,
                        trans * segment->point[j],
                        trans * segment->point[j + 1],
                        u);
                    if (dist < deltaTole && dist < minDist) {
                        minDist = dist;
                        ret = true;
                        u = segment->params[j]
                            + u * (segment->params[j + 1] - segment->params[j]);
                        const Base::Vector3d pp
                            = static_cast<Part::GeomCurve*>(geo)->value(u);
                        pos = { pp.x, pp.y };
                    }
                }
            }
        }
        if (!ret) {
            // Lowest priority: snap onto the background grid intersection.
            ret = snapToGridPoint(pos);
        }
        return ret;
    }
    void SketcherObjWidget::pickGeo()
    {
        onSketchPosMove = getMouseHitSketchPlanePoint();
        preSelectGeoId = testSelect(onSketchPosMove);
        std::set<int>avoidList;
        avoidList.insert(preSelectGeoId.GeoId);
        snapPoint(onSketchPosMove, avoidList);
    }
    void SketcherObjWidget::clearSelect() {
        selectIds.clear();
    }
    void SketcherObjWidget::moveGeo(SelectGeoId Id, float dx, float dy)
    {
        // Only the sketch's own geometry can be moved: an external reference is fixed
        // by definition (the solver is told so), and its curves are not even part of
        // m_sketch->geometries().
        if (Id.GeoId >= 0 && Id.GeoId < static_cast<int>(m_sketch->geometries().size())) {
            int geoId = Id.GeoId;
            Part::Geometry* geo = m_sketch->geometries()[geoId].get();
            bool isStart = Id.pointPos == PointPos::start;
            bool isEnd = Id.pointPos == PointPos::end;
            bool isCenter = Id.pointPos == PointPos::mid;
            bool isNone = Id.pointPos == PointPos::none;
            Base::Vector3d delta(dx, dy, 0);
            Base::Vector3d mousePos = Base::Vector3d(onSketchPosMove.x, onSketchPosMove.y, 0.0);

            {
                if (geo->isDerivedFrom<Part::GeomCurve>()) {
                    if (geo->is<Part::GeomArcOfCircle>()) {
                        Part::GeomArcOfCircle* curve = static_cast<Part::GeomArcOfCircle*>(geo);
                        if (isNone) {
                            curve->setRadius((mousePos - curve->getCenter()).Length());
                        }
                        else if (isCenter) {
                            curve->setCenter(mousePos);
                        }
                        else
                        {
                            // Keep the center and the radius fixed; only the
                            // grabbed endpoint travels along the circle. The
                            // stored range parameters are unbounded, so a
                            // signed [-pi, pi] mouse angle would jump whenever
                            // the arc crosses the +/-x axis. Compute the small
                            // angular delta between the current endpoint and
                            // the mouse and add it to the stored parameter to
                            // keep the update continuous.
                            constexpr double kPi = 3.14159265358979323846;
                            constexpr double kTwoPi = 2.0 * kPi;
                            const Base::Vector3d currentDir = (isStart
                                    ? curve->getStartPoint(true)
                                    : curve->getEndPoint(true))
                                - curve->getCenter();
                            const Base::Vector3d mouseDir = mousePos - curve->getCenter();
                            if (currentDir.Length() > 1.0e-9 && mouseDir.Length() > 1.0e-9) {
                                double curAngle = std::atan2(currentDir.y, currentDir.x);
                                double mouseAngle = std::atan2(mouseDir.y, mouseDir.x);
                                double delta = mouseAngle - curAngle;
                                if (delta > kPi) {
                                    delta -= kTwoPi;
                                }
                                else if (delta < -kPi) {
                                    delta += kTwoPi;
                                }

                                double u, v;
                                curve->getRange(u, v, true);
                                if (isStart) {
                                    double newU = u + delta;
                                    // Keep the CCW range valid (u < v and
                                    // v - u <= 2*pi) while the grabbed start
                                    // endpoint passes the end endpoint.
                                    if (newU >= v) {
                                        newU -= kTwoPi;
                                    }
                                    if (v - newU > kTwoPi) {
                                        newU += kTwoPi;
                                    }
                                    curve->setRange(newU, v, true);
                                }
                                else if (isEnd) {
                                    double newV = v + delta;
                                    if (newV <= u) {
                                        newV += kTwoPi;
                                    }
                                    if (newV - u > kTwoPi) {
                                        newV -= kTwoPi;
                                    }
                                    curve->setRange(u, newV, true);
                                }
                            }
                        }
                    }
                    else if (geo->is<Part::GeomLineSegment>()) {
                        Part::GeomLineSegment* lineSeg = static_cast<Part::GeomLineSegment*>(geo);
                        if (isStart) {
                            lineSeg->setPoints(mousePos, lineSeg->getEndPoint());
                        }
                        else if (isEnd) {
                            lineSeg->setPoints(lineSeg->getStartPoint(), mousePos);
                        }
                        else
                        {
                            geo->translate(delta);
                        }
                    }
                    else if (geo->is<Part::GeomArcOfConic>()) {
                        Part::GeomArcOfConic* curve = static_cast<Part::GeomArcOfConic*>(geo);

                    }
                    else if (geo->is<Part::GeomCircle>()) {
                        Part::GeomCircle* curve = static_cast<Part::GeomCircle*>(geo);
                        if (isNone) {
                            curve->setRadius((mousePos - curve->getCenter()).Length());
                        }
                        else
                        {
                            curve->setCenter(mousePos);
                        }
                    }
                    else if (geo->is<Part::GeomBSplineCurve>()) {
                        Part::GeomBSplineCurve* curve = static_cast<Part::GeomBSplineCurve*>(geo);
                        geo->translate(delta);
                    }
                    else if (geo->is<Part::GeomPoint>()) {
                        Part::GeomPoint* point = static_cast<Part::GeomPoint*>(geo);
                        if (isStart || isCenter || isNone) {
                            point->setPoint(mousePos);
                        }
                    }
                }
            }
            m_sketch->updateGeoSegment(geoId);
        }
    }
    void SketcherObjWidget::addSelect(SelectGeoId geoId)
    {
        bool existflag = false;
        for (int i = 0; i < selectIds.size(); i++) {
            if (selectIds[i].GeoId == geoId.GeoId && selectIds[i].pointPos == geoId.pointPos) {
                existflag = true;
                break;
            }
        }
        if (!existflag) {
            selectIds.push_back(geoId);
        }
    }
    Base::Vector2d SketcherObjWidget::getMouseHitSketchPlanePoint()
    {
        auto ray = m_sceneView->GetMouseRay();
        Maths::FVector3 out;
        Base::Vector2d onSketchPos;
        ray.hitPlane(Maths::FVector3(m_sketch->plane().normal.x, m_sketch->plane().normal.y, m_sketch->plane().normal.z), m_sketch->plane().normal.Dot(m_sketch->plane().origin), out);
        Base::Vector3d hitPos{ out.x,out.y,out.z };
        double x = (hitPos - m_sketch->plane().origin).Dot(m_sketch->plane().xAxis);
        double y = (hitPos - m_sketch->plane().origin).Dot(m_sketch->plane().yAxis);
        onSketchPos = Base::Vector2d(int(x * 100) / 100.0, int(y * 100) / 100.0);
        return onSketchPos;
    }
    void SketcherObjWidget::selectGeo(int geoId)
    {
        clearSelect();
        if (geoId >= 0 && geoId < static_cast<int>(m_sketch->geometries().size())) {
            addSelect({ geoId, PointPos::none });
        }
    }
    void SketcherObjWidget::setPreselect(int geoId)
    {
        preSelectGeoId = { geoId, PointPos::none };
    }
}




