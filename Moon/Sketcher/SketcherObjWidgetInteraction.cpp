#include "Sketcher/SketcherObjWidget.h"
#include "Sketcher/SketchPicking.h"
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
        // The tools of a sketch ask the data whether one is being edited; this is the
        // widget that makes that true, and nothing in the data knows who it is.
        if (m_sketch != nullptr) {
            m_sketch->setBeingEdited(true);
        }
        setActive(true);
    }
    SketcherObjWidget::~SketcherObjWidget()
    {
        if (m_sketch != nullptr) {
            m_sketch->setBeingEdited(false);
        }
    }
    void SketcherObjWidget::onMouseMove()
    {
        if (!isHaveActiveHandler && isInEdit) {
            updateConstraintLabelInteraction();
            // While the cursor rests on a dimension label (or drags one) the
            // mouse must not select/move the geometry underneath it.
            if (m_labelDrag >= 0 || m_labelHover != -1) {
                m_sketch->clearPreselect();
                return;
            }
        }
        onSketchPosP2 = onSketchPosMove;
        Base::Vector2d preOnSketchPosMove = onSketchPosMove;
        if (!isHaveActiveHandler && isInEdit) {
            pickGeo();
            if (selectState == Stop && m_sketch->getPreSelectGeoId().GeoId != NoGeoId) {
                selectState = Hot;
            }
            else if (selectState == OperationGeo) {
                const bool singleCircleArcDrag = [&]() {
                    if (m_sketch->getSelectGeoPosIds().size() != 1) {
                        return false;
                    }
                    Part::Geometry* geo = m_sketch->getGeometry(m_sketch->getSelectGeoPosIds()[0].GeoId);
                    return geo
                        && (geo->is<Part::GeomCircle>() || geo->is<Part::GeomArcOfCircle>());
                }();
                // Rim drags resize a circle/arc. They go through the solver
                // with an absolute target: the grabbed rim follows the mouse
                // while the center is pinned by a temporary coincidence, so
                // constrained composites (e.g. a slot cap) resize without the
                // center drifting.
                const bool radiusDrag
                    = singleCircleArcDrag && m_sketch->getSelectGeoPosIds()[0].pointPos == PointPos::none;

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
                    for (const auto& sel : m_sketch->getSelectGeoPosIds()) {
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
                dragIds.reserve(m_sketch->getSelectGeoPosIds().size());
                for (const auto& sel : m_sketch->getSelectGeoPosIds()) {
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
                for (int i = 0; i < m_sketch->getSelectGeoPosIds().size(); i++) {
                    moveGeo(
                        m_sketch->getSelectGeoPosIds()[i],
                        onSketchPosMove.x - preOnSketchPosMove.x,
                        onSketchPosMove.y - preOnSketchPosMove.y
                    );
                    solveS = true;
                }
                if (solveS) {
                    m_sketch->solve();
                }
            }
            else if (selectState == Hot && m_sketch->getPreSelectGeoId().GeoId == NoGeoId) {
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
                m_sketch->clearPreselect();
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
                m_sketch->clearPreselect();

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
        if (m_sketch->getPreSelectGeoId().GeoId == NoGeoId) {
            pickGeo();
        }
      
        if (!isHaveActiveHandler) {
            if (selectState == Hot) {
                if (m_sketch->getPreSelectGeoId().GeoId != NoGeoId) {
                    const bool alreadySelected = [this]() {
                        for (const auto& sel : m_sketch->getSelectGeoPosIds()) {
                            if (sel.GeoId == m_sketch->getPreSelectGeoId().GeoId
                                && sel.pointPos == m_sketch->getPreSelectGeoId().pointPos) {
                                return true;
                            }
                        }
                        return false;
                    }();
                    if (alreadySelected && m_sketch->getPreSelectGeoId().pointPos != PointPos::none) {
                        // Several points coincide; clicking the already
                        // selected one cycles to the next unselected point.
                        SelectGeoId next;
                        if (findNextCoincidentPoint(onSketchPosClicked, m_sketch->getPreSelectGeoId(), next)) {
                            if (selectMode == OverrideSelect) {
                                clearSelect();
                            }
                            addSelect(next);
                            m_sketch->setPreselect(next);
                            selectState = OperationGeo;
                            return;
                        }
                    }
                    if (selectMode == OverrideSelect) {
                        clearSelect();
                    }
                    addSelect(m_sketch->getPreSelectGeoId());
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
            std::vector<int>deletList(m_sketch->getSelectGeoPosIds().size());
            for (int i = 0; i < m_sketch->getSelectGeoPosIds().size(); i++) {
                deletList[i] = m_sketch->getSelectGeoPosIds()[i].GeoId;
            }
            m_sketch->deleteGeometries(deletList);
            m_sketch->clearSelect();
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
            for (const auto& sel : m_sketch->getSelectGeoPosIds()) {
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
        return m_sketch->getSelectIds();
    }

    void SketcherObjWidget::addSelect(int id)
    {
        m_sketch->addSelect(id);
    }

    void SketcherObjWidget::removeSelect(const std::vector<int>& idList)
    {
        m_sketch->removeSelect(idList);
    }
    void SketcherObjWidget::pickGeo()
    {
        onSketchPosMove = getMouseHitSketchPlanePoint();
        // Picking and snapping are queries of the view layer: the sketch data and the
        // camera say what is under the cursor.
        m_sketch->setPreselect(SketchPicking::testSelect(*m_sketch, *m_sceneView, onSketchPosMove));
        std::set<int> avoidList;
        avoidList.insert(m_sketch->getPreSelectGeoId().GeoId);
        SketchPicking::snapPoint(*m_sketch, *m_sceneView, *renderer, onSketchPosMove, avoidList);
    }
    void SketcherObjWidget::clearSelect() {
        m_sketch->clearSelect();
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
        m_sketch->addSelect(geoId);
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
        // The pick itself is data of the sketch; this only forwards it.
        m_sketch->selectGeo(geoId);
    }
    void SketcherObjWidget::setPreselect(int geoId)
    {
        m_sketch->setPreselect(geoId);
    }
}














