#include "Sketcher/SketcherObj.h"
#include "renderer/SceneView.h"
#include "editor/Toolbar/sketchToolbar.h"
#include "Core/Global/ServiceLocator.h"
#include <cmath>
namespace MOON {
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
    void SketcherObj::onMouseMove()
    {
        // Nothing of the sketch is being picked while external geometry is being
        // chosen; the scene highlight that the picking pass draws is the feedback.
        if (m_externalGeometryMode && !isHaveActiveHandler) {
            if (preSelectGeoId.GeoId != NoGeoId) {
                preSelectGeoId = { NoGeoId, PointPos::none };
            }
            selectState = Stop;
            return;
        }
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
                    Part::Geometry* geo = getGeometry(selectIds[0].GeoId);
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
                        // Normalize curves added since the last solve() so the
                        // range angles read by moveGeo() are canonical.
                        solve();
                        m_dragSolverInit = true;
                    }
                    for (const auto& sel : selectIds) {
                        moveGeo(
                            sel,
                            static_cast<float>(onSketchPosMove.x - preOnSketchPosMove.x),
                            static_cast<float>(onSketchPosMove.y - preOnSketchPosMove.y)
                        );
                    }
                    solve();
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
                    // Rebuild the solver state from the current geometry list
                    // before anchoring the drag. Curves committed after the
                    // last solve would otherwise be missing from solvedSketch
                    // and would disappear when the drag rebuilds the list.
                    solve();
                    solvedSketch.resetInitMove();
                    m_dragSolverInit = solvedSketch.initMove(dragIds) == 0;
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
                    const int status = solvedSketch.moveGeometries(dragIds, moveTo, relative);
                    if (status == 0) {
                        for (auto& geo : mGeoList) {
                            mGeoSegment.erase(geo.get());
                        }
                        mGeoList.clear();
                        std::vector<Part::Geometry*> geomlist = solvedSketch.extractGeometry();
                        for (Part::Geometry* geo : geomlist) {
                            addGeometry(geo);
                        }
                        for (Part::Geometry* geo : geomlist) {
                            delete geo;
                        }
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
                    solve();
                }
            }
            else if (selectState == Hot && preSelectGeoId.GeoId == NoGeoId) {
                selectState = Stop;
            }
        }
    }

    void SketcherObj::onLeftMousePressed()
    {
        // External geometry mode: the click belongs to the scene, not to the sketch.
        // The pick pass has already resolved the actor under the cursor (the scene
        // view keeps the last hover pick), so what is under the mouse is what is
        // referenced - including the highlight the scene draws for it.
        if (m_externalGeometryMode && !isHaveActiveHandler) {
            pickExternalGeometry();
            return;
        }
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
                m_labelDragPressPx = Base::Vector2d(mx, my);
                m_lastLabelClick = -1;
                clearSelect();
                selectState = Stop;
                preSelectGeoId = { NoGeoId, PointPos::none };
                return;
            }
            const int labelHit = annotationHit;
            if (labelHit >= 0) {
                const Sketcher::Constraint* c = getConstraint(labelHit);
                m_labelDragHandle = LabelHandle::Caption;
                float defDx = 0.0f, defDy = 0.0f;
                defaultLabelOffsetPx(c, defDx, defDy);
                const auto it = c ? m_labelManualOffsetPx.find(c) : m_labelManualOffsetPx.end();
                m_labelDragOffsetPx = (it != m_labelManualOffsetPx.end())
                    ? it->second
                    : Base::Vector2d(defDx, defDy);
                m_labelDrag = labelHit;
                m_labelDragPressPx = Base::Vector2d(mx, my);
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
    void SketcherObj::onLeftMouseReleased()
    {
        if (m_labelDrag >= 0) {
            // A dimension label was being dragged; the label position is kept
            // in m_labelManualOffsetPx, so simply end the drag here.
            m_labelDrag = -1;
            return;
        }
        sketchDrawRect = false;
        onSketchPosP2 = getMouseHitSketchPlanePoint();
        if (!isHaveActiveHandler) {
            if (selectState == OperationGeo) {
                selectState = Hot;
                m_dragSolverInit = false;
                solvedSketch.resetInitMove();
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
			    for (int i = 0;i < mGeoList.size();i++) {
                    if (mHiddenGeoIds.count(i)) {
                        continue;  // hidden geometry is not selectable
                    }
                    if (mConstructionGeoIds.count(i)) {
                        continue;  // construction aids are not user selectable
                    }
				    auto& seg = mGeoSegment[mGeoList[i].get()];
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
                selectState = Stop;
            }
        }
    }
    void SketcherObj::onKeyPress(const std::string& key)
    {
        if (key == "ESCAPE" && m_externalGeometryMode) {
            setExternalGeometryMode(false);
            GetService(SketchToolbar).uncheckExternalGeometry();
            return;
        }
        if (key == "DELETE" && !isHaveActiveHandler) {
            std::vector<int>deletList(selectIds.size());
            for (int i = 0; i < selectIds.size(); i++) {
                deletList[i] = selectIds[i].GeoId;
            }
            deleteGeometries(deletList);
            selectIds.clear();
            solve();
        }
        else if (key == "CONTROL_L") {
            selectMode = AppendSelect;
        }
    }
    void SketcherObj::onKeyRelease(const std::string& key)
    {
        if (key == "CONTROL_L") {
            selectMode = OverrideSelect;
        }
    }
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
    int SketcherObj::getPickGeoIndex(const Base::Vector2d& pos, const Base::Matrix4D& mat)
    {

        Base::Matrix4D trans = mat * planeTransform;
        Base::Vector3d p1 = trans * Base::Vector3d(pos.x, pos.y, 0);

        int ret = -1;
        double deltaTole = 15.0;
        double minDist = 10000.0;
        // 遍历所有几何图元
        for (int i = 0; i < mGeoList.size(); i++) {
            if (mHiddenGeoIds.count(i)) {
                continue;  // hidden geometry is not pickable
            }
            Part::Geometry* geo = mGeoList[i].get();
            if (geo->isDerivedFrom<Part::GeomCurve>()) {
                auto& segment = mGeoSegment[geo];
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
    SketcherObj::SelectGeoId SketcherObj::testSelect(const Base::Vector2d& pos)
    {
        Maths::FMatrix4 mat = m_sceneView->GetCamera()->GetViewPortMatrix();
        Base::Matrix4D viewPortMat(
            mat.data[0], mat.data[1], mat.data[2], mat.data[3],
            mat.data[4], mat.data[5], mat.data[6], mat.data[7],
            mat.data[8], mat.data[9], mat.data[10], mat.data[11],
            mat.data[12], mat.data[13], mat.data[14], mat.data[15]
        );
        Base::Matrix4D trans = viewPortMat * getplaneTransform();
        Base::Vector3d p1 = trans * Base::Vector3d{ pos.x,pos.y,0.0 };
       double deltaTole = 5.0;
       double minDist = 10000.0;
       SelectGeoId ret = { NoGeoId,PointPos::none };
       // A hit has to be tracked separately: the first external curve carries the id
       // -1, so the id cannot double as "nothing was hit".
       bool hit = false;

        // The candidates are the sketch's own curves plus the projected external ones:
        // an external curve is a reference the user may constrain to, so it has to be
        // selectable - but only outside a drawing tool, where a reference must not be
        // mistaken for the geometry the tool is working on.
        std::vector<std::pair<int, Part::Geometry*>> candidates;
        candidates.reserve(mGeoList.size() + getExternalCurveCount());
        for (int i = 0; i < static_cast<int>(mGeoList.size()); ++i) {
            if (mHiddenGeoIds.count(i)) {
                continue;
            }
            candidates.emplace_back(i, mGeoList[i].get());
        }
        if (!isHaveActiveHandler) {
            for (int i = 0; i < getExternalCurveCount(); ++i) {
                Part::Geometry* geo = const_cast<Part::Geometry*>(
                    getExternalCurve(getExternalGeoId(i)));
                // Only a curve that has been sampled can be hit; a missing cache entry
                // must not be turned into an empty one here.
                if (geo != nullptr && findSegment(geo) != nullptr) {
                    candidates.emplace_back(getExternalGeoId(i), geo);
                }
            }
        }

        // travel all segments
        for (const auto& [geoId, geo] : candidates) {
            auto& segment = segmentOf(geo);
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
                auto& segment = segmentOf(geo);
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
       return ret;
    }
    bool SketcherObj::findNextCoincidentPoint(
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
        const Base::Matrix4D trans = viewPortMat * getplaneTransform();
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

        for (int i = 0; i < static_cast<int>(mGeoList.size()); ++i) {
            if (mHiddenGeoIds.count(i)) {
                continue;
            }
            const auto segIt = mGeoSegment.find(mGeoList[i].get());
            if (segIt == mGeoSegment.end()) {
                continue;
            }
            const auto& sePoints = segIt->second.sepoints;
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
    std::vector<int> SketcherObj::getSelectIds() const
    {
        std::vector<int>selectIdLists(selectIds.size());
        for (int i = 0; i < selectIds.size(); i++) {
            selectIdLists[i] = selectIds[i].GeoId;
        }
        return selectIdLists;
    }

    void SketcherObj::addSelect(int id)
    {
        addSelect({ id,PointPos::none });
    }

    void SketcherObj::removeSelect(const std::vector<int>& idList)
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
    bool SketcherObj::snapPoint(Base::Vector2d& pos, const std::set<int>& avoid)
    {
        Maths::FMatrix4 mat = m_sceneView->GetCamera()->GetViewPortMatrix();
        Base::Matrix4D pla(
            mat.data[0], mat.data[1], mat.data[2], mat.data[3],
            mat.data[4], mat.data[5], mat.data[6], mat.data[7],
            mat.data[8], mat.data[9], mat.data[10], mat.data[11],
            mat.data[12], mat.data[13], mat.data[14], mat.data[15]
        );
        Base::Matrix4D trans = pla * getplaneTransform();
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
            for (int i = 0; i < getExternalCurveCount(); ++i) {
                const int geoId = getExternalGeoId(i);
                if (avoid.count(geoId)) {
                    continue;
                }
                Part::Geometry* geo = const_cast<Part::Geometry*>(getExternalCurve(geoId));
                if (geo == nullptr) {
                    continue;
                }
                const CurveSegment* segment = findSegment(geo);
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
        for (int i = 0; i < mGeoList.size(); i++) {
            if (!avoid.count(i)) {
                if (mConstructionGeoIds.count(i)) {
                    continue;  // construction aids are not snap targets
                }
                if (mHiddenGeoIds.count(i)) {
                    continue;  // hidden geometry is not a snap target
                }
                Part::Geometry* geo = mGeoList[i].get();
                auto& segment = mGeoSegment[geo];
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
            for (int i = 0; i < mGeoList.size(); i++) {
                if (!avoid.count(i)) {
                    if (mConstructionGeoIds.count(i)) {
                        continue;
                    }
                    if (mHiddenGeoIds.count(i)) {
                        continue;
                    }
                    Part::Geometry* geo = mGeoList[i].get();
                    auto& segment = mGeoSegment[geo];
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
            for (int i = 0; i < getExternalCurveCount(); ++i) {
                const int geoId = getExternalGeoId(i);
                if (avoid.count(geoId)) {
                    continue;
                }
                Part::Geometry* geo = const_cast<Part::Geometry*>(getExternalCurve(geoId));
                if (geo == nullptr || !geo->isDerivedFrom<Part::GeomCurve>()) {
                    continue;
                }
                const CurveSegment* segment = findSegment(geo);
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
        m_labelManualOffsetPx.clear();
        m_labelManualParam.clear();
        m_straightDimOffsetPx.clear();
        m_labelHover = -1;
        m_labelDrag = -1;
        m_labelHoverHandle = LabelHandle::Caption;
        m_labelDragHandle = LabelHandle::Caption;
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
    void SketcherObj::pickGeo()
    {
        onSketchPosMove = getMouseHitSketchPlanePoint();
        preSelectGeoId = testSelect(onSketchPosMove);
        std::set<int>avoidList;
        avoidList.insert(preSelectGeoId.GeoId);
        snapPoint(onSketchPosMove, avoidList);
    }
    void SketcherObj::clearSelect() {
        selectIds.clear();
    }
    void SketcherObj::moveGeo(SelectGeoId Id, float dx, float dy)
    {
        // Only the sketch's own geometry can be moved: an external reference is fixed
        // by definition (the solver is told so), and its curves are not even part of
        // mGeoList.
        if (Id.GeoId >= 0 && Id.GeoId < static_cast<int>(mGeoList.size())) {
            int geoId = Id.GeoId;
            Part::Geometry* geo = mGeoList[geoId].get();
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
            updateGeoSegment(geoId);
        }
    }
    void SketcherObj::addSelect(SelectGeoId geoId)
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
    Base::Vector2d SketcherObj::getMouseHitSketchPlanePoint()
    {
        auto ray = m_sceneView->GetMouseRay();
        Maths::FVector3 out;
        Base::Vector2d onSketchPos;
        ray.hitPlane(Maths::FVector3(mPlane.normal.x, mPlane.normal.y, mPlane.normal.z), mPlane.normal.Dot(mPlane.origin), out);
        Base::Vector3d hitPos{ out.x,out.y,out.z };
        double x = (hitPos - mPlane.origin).Dot(mPlane.xAxis);
        double y = (hitPos - mPlane.origin).Dot(mPlane.yAxis);
        onSketchPos = Base::Vector2d(int(x * 100) / 100.0, int(y * 100) / 100.0);
        return onSketchPos;
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
    void SketcherObj::setGeometryVisible(int geoId, bool visible)
    {
        if (visible) {
            mHiddenGeoIds.erase(geoId);
        }
        else {
            mHiddenGeoIds.insert(geoId);
        }
    }
    void SketcherObj::selectGeo(int geoId)
    {
        clearSelect();
        if (geoId >= 0 && geoId < static_cast<int>(mGeoList.size())) {
            addSelect({ geoId, PointPos::none });
        }
    }
    void SketcherObj::setPreselect(int geoId)
    {
        preSelectGeoId = { geoId, PointPos::none };
    }
}
