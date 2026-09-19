#include "Sketcher/SketcherObj.h"
#include "Interactive/Widgets/DrawSketchHandler.h"
#include "Geometry.h"
#include "renderer/SceneView.h"
#include "Interactive/Im3DRenderer.h"
#include "Core/Global/ServiceLocator.h"
#include "core/log.h"
#include "Qtimgui/imgui/imgui.h"
#include "Qtimgui/implot/implotCustom.h"
#include "Sketcher/SketcheTool2D.h"
#include "editor/Toolbar/sketchToolbar.h"
#include <QInputDialog>
namespace MOON {
    // Constraint types that get a numeric viewport label.
    static bool isDimensionLabelType(Sketcher::ConstraintType type)
    {
        return type == Sketcher::ConstraintType::Distance
            || type == Sketcher::ConstraintType::DistanceX
            || type == Sketcher::ConstraintType::DistanceY
            || type == Sketcher::ConstraintType::Radius
            || type == Sketcher::ConstraintType::Diameter
            || type == Sketcher::ConstraintType::Angle;
    }
    static ImU32 abgrToImU32(const Eigen::Vector4<uint8_t>& c)
    {
        return IM_COL32(c[3], c[2], c[1], c[0]);
    }
    // Draws a sketch-plane polyline with a screen-space dashed pattern (dash
    // and gap lengths are fixed in pixels, so the pattern keeps its size when
    // the view is zoomed). Construction geometry uses this style.
    static void drawDashedSketchPolyline(
        ImRenderer* renderer,
        SketcherPlane2D& plane,
        const std::vector<Base::Vector3d>& points,
        float dashPx = 8.0f,
        float gapPx = 6.0f
    )
    {
        if (!renderer || points.size() < 2) {
            return;
        }
        const float cycle = dashPx + gapPx;
        float phase = 0.0f;
        for (int i = 0; i + 1 < static_cast<int>(points.size()); i++) {
            const Eigen::Vector3f wa = plane.valueEigen(points[i].x, points[i].y);
            const Eigen::Vector3f wb = plane.valueEigen(points[i + 1].x, points[i + 1].y);
            const Eigen::Vector2f aS = renderer->worldToScreen(wa);
            const Eigen::Vector2f bS = renderer->worldToScreen(wb);
            const float len = (bS - aS).norm();
            if (len < 0.05f) {
                continue;
            }
            float t = 0.0f;
            while (t < len - 0.1f) {
                const bool ink = phase < dashPx;
                const float stateRemain = ink ? (dashPx - phase) : (cycle - phase);
                const float step = std::min(stateRemain, len - t);
                if (ink && step > 0.1f) {
                    const float u0 = t / len;
                    const float u1 = (t + step) / len;
                    renderer->drawLine(wa + (wb - wa) * u0, wa + (wb - wa) * u1);
                }
                t += step;
                phase += step;
                if (phase >= cycle) {
                    phase -= cycle;
                }
            }
        }
    }
    // Default screen-space offset (in pixels) applied to the auto anchor when
    // the user has not moved the label manually.
    void defaultLabelOffsetPx(const Sketcher::Constraint* c, float& dx, float& dy)
    {
        dx = 0.0f;
        dy = -22.0f;  // by default put the text above the measured geometry
        if (!c) {
            return;
        }
        if (c->Type == Sketcher::ConstraintType::DistanceY) {
            dx = 26.0f;  // vertical distances read better on the right side
            dy = 0.0f;
        }
        else if (c->Type == Sketcher::ConstraintType::Radius
            || c->Type == Sketcher::ConstraintType::Diameter) {
            dx = 22.0f;  // diagonal offset away from the center/rim
            dy = -22.0f;
        }
    }
    static bool isCircleArcGeometry(const Part::Geometry* geo)
    {
        return geo && (geo->is<Part::GeomCircle>() || geo->is<Part::GeomArcOfCircle>());
    }
    static bool getCircleArcInfo(
        const Part::Geometry* geo,
        Base::Vector2d& center,
        double& radius
    )
    {
        if (!geo) {
            return false;
        }
        if (geo->is<Part::GeomCircle>()) {
            const auto* circle = static_cast<const Part::GeomCircle*>(geo);
            const Base::Vector3d c = circle->getCenter();
            center.x = c.x;
            center.y = c.y;
            radius = circle->getRadius();
            return true;
        }
        if (geo->is<Part::GeomArcOfCircle>()) {
            const auto* arc = static_cast<const Part::GeomArcOfCircle*>(geo);
            const Base::Vector3d c = arc->getCenter();
            center.x = c.x;
            center.y = c.y;
            radius = arc->getRadius();
            return true;
        }
        return false;
    }
    void SketcherObj::setPlane(const SketcherPlane2D& plane)
    {
        mPlane = plane;
        // External geometry is projected into the sketch plane, so it has to be
        // projected again when the plane moves.
        for (ExternalGeometry& external : mExternalGeometry) {
            external.dirty = true;
        }
        fitCamera();
        //GetService(SketchToolbar).disableAllHandlers();
    }
    void SketcherObj::fitCamera()
    {
        auto& view = GetService(Editor::Panels::SceneView);
        view.GetCameraController().EnableRotate(false);
        view.GetCamera()->SetSize(100);
        view.GetCamera()->SetProjectionMode(Rendering::Settings::EProjectionMode::ORTHOGRAPHIC);
        float pos = view.GetCamera()->GetFar() / 2.0;
        Maths::FVector3 normal(mPlane.normal.x, mPlane.normal.y, mPlane.normal.z);
        Maths::FVector3 up(mPlane.yAxis.x, mPlane.yAxis.y, mPlane.yAxis.z);
        Maths::FQuaternion quat = Maths::FQuaternion::LookAt(-normal, up);
        view.GetCameraController().MoveToPose(Maths::FVector3(mPlane.origin.x, mPlane.origin.y, mPlane.origin.z) + normal * pos, quat);
        planeTransform = updateTransform();
    }
    void SketcherObj::beginEdit()
    {
        isInEdit = true;
        setActive(true);
        fitCamera();
    }
    void SketcherObj::onSetActive(bool flag)
    {
        if (!flag) {
            // Leaving the sketch leaves its tools: the external geometry button has to
            // follow, otherwise it would still be pressed the next time the sketch is
            // opened (and the clicks of the next tool would be taken by it).
            setExternalGeometryMode(false);
            GetService(SketchToolbar).uncheckExternalGeometry();
        }
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
    void SketcherObj::onUpdate()
    {
        isHaveActiveHandler = false;
        auto& gizmoWidgets = renderer->getGizmoWidgets();
        for (auto& it : gizmoWidgets) {
            if (it.second->isActived() && dynamic_cast<DrawSketchHandler*>(it.second)) {
                isHaveActiveHandler = true;
                break;
            }
        }
        if (isHaveActiveHandler) {
            // A geometry-drawing tool takes over the mouse; stop any label
            // hover/drag so it cannot fight with the active handler.
            m_labelHover = -1;
            m_labelDrag = -1;
            // The preselect is not refreshed while a handler runs, and the handlers
            // only ever work on the sketch's own geometry: an external id left over
            // from before the tool was started would be read as a curve of this
            // sketch by them.
            if (preSelectGeoId.GeoId < 0) {
                preSelectGeoId = { NoGeoId, PointPos::none };
            }
        }
        draw();
    }
    void SketcherObj::getPlaneNormal(double* p)
    {
        p[0] = mPlane.normal.x;
        p[1] = mPlane.normal.y;
        p[2] = mPlane.normal.z;
    }
    void SketcherObj::drawBackground()
    {
        if (!InEdit()) return;

        // Adaptive background grid: big cells use the darker tone, the smaller
        // subdivisions inside use the lighter tone. The spacing snaps to a nice
        // 1/2/5 x 10^n step so the on-screen density stays roughly constant while
        // zooming, and each line spans the whole visible sketch region.
        if (m_drawGrid) {
            const Eigen::Vector4<uint8_t> minorColor(150, 132, 118, 118); // (A,B,G,R) = (255, b,g,r)
            const Eigen::Vector4<uint8_t> majorColor(255, 66, 56, 56); // (A,B,G,R) = (255, b,g,r)
            const auto* gridCam = m_sceneView->GetCamera();
            if (gridCam) {
                const auto& gProj = gridCam->GetProjectionMatrix();
                const float gProj11 = gProj(1, 1);
                const auto& gFd = m_sceneView->GetRenderer().GetFrameDescriptor();
                const float gScreenH = static_cast<float>(gFd.renderHeight);
                const float gScreenW = static_cast<float>(gFd.renderWidth);
                const float gAspect = gScreenH > 0.0f ? gScreenW / gScreenH : 1.0f;
                if (gProj11 > 0.0f && gScreenH > 0.0f &&
                    gridCam->GetProjectionMode() == ::Rendering::Settings::EProjectionMode::ORTHOGRAPHIC) {
                    const float halfH = 1.0f / gProj11;
                    const float hx = halfH * gAspect;
                    const float hy = halfH;
                    // Visible sketch-space rectangle: project the four viewport corners
                    // onto the sketch plane (view z is irrelevant for an ortho camera).
                    const auto& gView = gridCam->GetViewMatrix();
                    const Maths::FMatrix4 gInvView = Maths::FMatrix4::Inverse(gView);
                    const float xs[2] = { -hx, hx };
                    const float ys[2] = { -hy, hy };
                    float uMin = 1e30f, uMax = -1e30f, vMin = 1e30f, vMax = -1e30f;
                    for (int i = 0; i < 2; ++i) {
                        for (int j = 0; j < 2; ++j) {
                            const Maths::FVector3 wp = gInvView.MulPoint(Maths::FVector3(xs[i], ys[j], 0.0f));
                            const Base::Vector3d d(wp.x - mPlane.origin.x, wp.y - mPlane.origin.y, wp.z - mPlane.origin.z);
                            const float u = static_cast<float>(d.Dot(mPlane.xAxis));
                            const float v = static_cast<float>(d.Dot(mPlane.yAxis));
                            uMin = std::min(uMin, u); uMax = std::max(uMax, u);
                            vMin = std::min(vMin, v); vMax = std::max(vMax, v);
                        }
                    }
                    // Nice step: smallest 1/2/5 x 10^n candidate above the target so the
                    // minor lines are roughly 40 px apart on screen.
                    const float targetStep = 40.0f * (2.0f * halfH) / gScreenH;
                    const float mag = std::pow(10.0f, std::floor(std::log10(std::max(targetStep, 1e-6f))));
                    float step = mag;
                    if (step < targetStep) step = 2.0f * mag;
                    if (step < targetStep) step = 5.0f * mag;
                    if (step < targetStep) step = 10.0f * mag;
                    const int i0 = static_cast<int>(std::ceil(uMin / step));
                    const int i1 = static_cast<int>(std::floor(uMax / step));
                    for (int i = i0; i <= i1; ++i) {
                        const float g = i * step;
                        const auto& col = (i % 5) == 0 ? majorColor : minorColor;
                        renderer->drawLine(mPlane.valueEigen(g, vMin), mPlane.valueEigen(g, vMax), 1.0f, col);
                    }
                    const int j0 = static_cast<int>(std::ceil(vMin / step));
                    const int j1 = static_cast<int>(std::floor(vMax / step));
                    for (int j = j0; j <= j1; ++j) {
                        const float g = j * step;
                        const auto& col = (j % 5) == 0 ? majorColor : minorColor;
                        renderer->drawLine(mPlane.valueEigen(uMin, g), mPlane.valueEigen(uMax, g), 1.0f, col);
                    }
                }
                else {
                    // Fallback for non-orthographic views: fixed extent grid.
                    const float extent = 100.0f;
                    for (int k = -10; k <= 10; ++k) {
                        const float g = k * 10.0f;
                        const auto& col = (k % 5) == 0 ? majorColor : minorColor;
                        renderer->drawLine(mPlane.valueEigen(g, -extent), mPlane.valueEigen(g, extent), 1.0f, col);
                        renderer->drawLine(mPlane.valueEigen(-extent, g), mPlane.valueEigen(extent, g), 1.0f, col);
                    }
                }
            }
        }

        // Infinite X/Y axes: intersect each axis line with the viewport rectangle
        // and draw the segment between the two crossings so they span the screen.
        renderer->pushSize(3);
        const auto* axisCam = m_sceneView->GetCamera();
        if (axisCam) {
            const auto& proj = axisCam->GetProjectionMatrix();
            const float proj11 = proj(1, 1);
            const auto& fd = m_sceneView->GetRenderer().GetFrameDescriptor();
            const float screenH = static_cast<float>(fd.renderHeight);
            const float aspect = screenH > 0.0f ? static_cast<float>(fd.renderWidth) / screenH : 1.0f;
            if (proj11 > 0.0f && axisCam->GetProjectionMode() == ::Rendering::Settings::EProjectionMode::ORTHOGRAPHIC) {
                const auto& viewM = axisCam->GetViewMatrix();
                const Maths::FVector3 origin3(mPlane.origin.x, mPlane.origin.y, mPlane.origin.z);
                const Maths::FVector3 v0 = viewM.MulPoint(origin3);
                const float halfH = 1.0f / proj11;
                const float hx = halfH * aspect;
                const float hy = halfH;
                const Base::Vector3d dirs[2] = { mPlane.xAxis, mPlane.yAxis };
                const Eigen::Vector4<uint8_t> colors[2] = { {255, 0, 0, 255}, {255, 0, 255, 0} };
                for (int a = 0; a < 2; ++a) {
                    const Maths::FVector3 p1 = viewM.MulPoint(Maths::FVector3(
                        mPlane.origin.x + dirs[a].x, mPlane.origin.y + dirs[a].y, mPlane.origin.z + dirs[a].z));
                    const Maths::FVector3 dv(p1.x - v0.x, p1.y - v0.y, 0.0f);
                    const float coords[2] = { v0.x, v0.y };
                    const float dirC[2] = { dv.x, dv.y };
                    const float half[2] = { hx, hy };
                    float lo = -1e9f, hi = 1e9f;
                    bool crosses = true;
                    for (int c = 0; c < 2; ++c) {
                        if (std::abs(dirC[c]) < 1e-9f) {
                            if (std::abs(coords[c]) > half[c]) { crosses = false; break; }
                        }
                        else {
                            const float t1 = (-half[c] - coords[c]) / dirC[c];
                            const float t2 = (half[c] - coords[c]) / dirC[c];
                            lo = std::max(lo, std::min(t1, t2));
                            hi = std::min(hi, std::max(t1, t2));
                        }
                    }
                    if (crosses && hi >= lo) {
                        const float ext = std::max((hi - lo) * 0.05f, 1e-3f);
                        renderer->pushColor(colors[a]);
                        if (a == 0) {
                            renderer->drawLine(mPlane.valueEigen(lo - ext, 0.0), mPlane.valueEigen(hi + ext, 0.0));
                        }
                        else {
                            renderer->drawLine(mPlane.valueEigen(0.0, lo - ext), mPlane.valueEigen(0.0, hi + ext));
                        }
                        renderer->popColor();
                    }
                }
            }
            else {
                renderer->pushColor({ 255,0,0,255 });
                renderer->drawLine(mPlane.valueEigen(500, 0), mPlane.valueEigen(-500, 0));
                renderer->popColor();
                renderer->pushColor({ 255,0,255,0 });
                renderer->drawLine(mPlane.valueEigen(0, 500), mPlane.valueEigen(0, -500));
                renderer->popColor();
            }
        }
        renderer->popSize();

        // Origin marker.
        renderer->pushColor({ 255,255,255,0 });
        renderer->drawPoint(mPlane.valueEigen(0, 0));
        renderer->popColor();
    }
    bool SketcherObj::snapToGridPoint(Base::Vector2d& pos) const
    {
        if (!m_snapToGrid || !m_drawGrid || !m_sceneView) {
            return false;
        }
        // Reuse the same adaptive step as drawBackground(): minor grid lines
        // are about 40 px apart, snapped to a nice 1/2/5 x 10^n value.
        const auto* camera = m_sceneView->GetCamera();
        if (!camera) {
            return false;
        }
        const auto& proj = camera->GetProjectionMatrix();
        const float proj11 = proj(1, 1);
        const auto& fd = m_sceneView->GetRenderer().GetFrameDescriptor();
        const float screenH = static_cast<float>(fd.renderHeight);
        if (proj11 <= 0.0f || screenH <= 0.0f
            || camera->GetProjectionMode() != ::Rendering::Settings::EProjectionMode::ORTHOGRAPHIC) {
            return false;
        }
        const float halfH = 1.0f / proj11;
        const float targetStep = 40.0f * (2.0f * halfH) / screenH;
        if (targetStep <= 1.0e-6f) {
            return false;
        }
        const float mag = std::pow(
            10.0f,
            std::floor(std::log10(std::max(targetStep, 1.0e-6f)))
        );
        float step = mag;
        if (step < targetStep) {
            step = 2.0f * mag;
        }
        if (step < targetStep) {
            step = 5.0f * mag;
        }
        if (step < targetStep) {
            step = 10.0f * mag;
        }
        const double gx = std::round(pos.x / step) * step;
        const double gy = std::round(pos.y / step) * step;
        const auto worldOf = [this](const Base::Vector2d& sk) {
            return mPlane.origin + sk.x * mPlane.xAxis + sk.y * mPlane.yAxis;
        };
        const auto screenOf = [this, &worldOf](const Base::Vector2d& sk) {
            const Base::Vector3d w = worldOf(sk);
            return renderer->worldToScreen(
                Eigen::Vector3f(
                    static_cast<float>(w.x),
                    static_cast<float>(w.y),
                    static_cast<float>(w.z)
                )
            );
        };
        const Eigen::Vector2f cursorS = screenOf(pos);
        const Eigen::Vector2f gridS = screenOf(Base::Vector2d(gx, gy));
        const float screenDx = cursorS.x() - gridS.x();
        const float screenDy = cursorS.y() - gridS.y();
        // Screen-space distance threshold, independent of the grid step.
        constexpr float kSnapPixels = 10.0f;
        if (screenDx * screenDx + screenDy * screenDy
            <= kSnapPixels * kSnapPixels) {
            pos.x = gx;
            pos.y = gy;
            return true;
        }
        return false;
    }

    void SketcherObj::draw() {
        if (InEdit()) {
            drawBackground();
        }
        renderer->pushSize(m_drawOption.curveLineWidth);
       
        if (selectState == DragRect && sketchDrawRect && !isHaveActiveHandler) {
            Eigen::Vector3f p1 = mPlane.valueEigen(Base::Vector2d(std::min(onSketchPosP1.x, onSketchPosP2.x), std::min(onSketchPosP1.y, onSketchPosP2.y)));
            Eigen::Vector3f p2 = mPlane.valueEigen(Base::Vector2d(std::max(onSketchPosP1.x, onSketchPosP2.x), std::min(onSketchPosP1.y, onSketchPosP2.y)));
            Eigen::Vector3f p3 = mPlane.valueEigen(Base::Vector2d(std::max(onSketchPosP1.x, onSketchPosP2.x), std::max(onSketchPosP1.y, onSketchPosP2.y)));
            Eigen::Vector3f p4 = mPlane.valueEigen(Base::Vector2d(std::min(onSketchPosP1.x, onSketchPosP2.x), std::max(onSketchPosP1.y, onSketchPosP2.y)));
            renderer->drawQuad(p1, p2, p3, p4);
        }
        const Eigen::Vector4<uint8_t>& pointColor = m_drawOption.pointColor;
        const Eigen::Vector4<uint8_t>& preselectColor = m_drawOption.preselectColor;
        const Eigen::Vector4<uint8_t>& selectColor = m_drawOption.selectColor;
        const float pointSize = m_drawOption.pointSize;
        for (int i = 0;i < mGeoList.size();i++) {
            if (mHiddenGeoIds.count(i)) {
                continue;
            }
            bool isSelect = false;
            for (int j = 0;j < selectIds.size();j++) {
                if (selectIds[j].GeoId == i) {
                    if (selectIds[j].pointPos == PointPos::none) {
                        isSelect = true;
                    }
                }
            }
            auto& geo = mGeoList[i];
            const bool isConstruction
                = mConstructionGeoIds.count(i) != 0 || geo->getConstruction();
            if (isSelect) {
                renderer->pushColor(selectColor);
            }
            else if (i == preSelectGeoId.GeoId && selectState != OperationGeo) {
                renderer->pushColor(preselectColor);
            }
            else {
                renderer->pushColor(
                    isConstruction ? m_drawOption.constructionColor : m_drawOption.curveColor
                );
            }
            if (geo->isDerivedFrom<Part::GeomCurve>()) {
                auto& seg = mGeoSegment[geo.get()];
                if (isConstruction) {
                    drawDashedSketchPolyline(renderer, mPlane, seg.point);
                }
                else {
                    for (int k = 0; k + 1 < static_cast<int>(seg.point.size()); k++) {
                        renderer->drawLine(
                            mPlane.valueEigen(seg.point[k].x, seg.point[k].y),
                            mPlane.valueEigen(seg.point[k + 1].x, seg.point[k + 1].y)
                        );
                    }
                }
            }
            renderer->popColor();
        }
        // Geometry projected in from another feature: drawn in its own colour and
        // always solid, because it is a reference - the sketch may constrain to it but
        // never edits it, and it must not be mistaken for something drawn here.
        // External geometry is selectable like the sketch's own curves (it is what a
        // constraint is applied to), so it is highlighted like them - only its idle
        // colour sets it apart as a reference.
        for (int externalIndex = 0; externalIndex < getExternalCurveCount(); ++externalIndex) {
            const int geoId = getExternalGeoId(externalIndex);
            const Part::Geometry* externalGeo = getExternalCurve(geoId);
            if (externalGeo == nullptr) {
                continue;
            }
            const bool isSelected = [this, geoId]() {
                for (const SelectGeoId& sel : selectIds) {
                    if (sel.GeoId == geoId) {
                        return true;
                    }
                }
                return false;
                }();
            const bool isPreSelected = preSelectGeoId.GeoId == geoId
                && selectState != OperationGeo;

            auto& segment = segmentOf(const_cast<Part::Geometry*>(externalGeo));

            // The curve first, its markers on top: the same order the sketch's own
            // geometry is drawn in, because a marker under its own curve is invisible.
            renderer->pushColor(isSelected ? selectColor
                                           : (isPreSelected ? preselectColor
                                                            : m_drawOption.externalColor));
            if (externalGeo->isDerivedFrom<Part::GeomCurve>()) {
                // Sampled on demand: the external curves are rebuilt whenever their
                // source changes, and being few they can share the segment cache the
                // sketch geometry already uses.
                for (int k = 0; k + 1 < static_cast<int>(segment.point.size()); k++) {
                    renderer->drawLine(
                        mPlane.valueEigen(segment.point[k].x, segment.point[k].y),
                        mPlane.valueEigen(segment.point[k + 1].x, segment.point[k + 1].y)
                    );
                }
            }
            renderer->popColor();

            // The markers are what the user snaps to and constrains against, so they
            // are drawn as points - endpoints and centres, like the sketch's own.
            for (int k = 0; k < static_cast<int>(segment.sepoints.size()); ++k) {
                const PointPos pos = segment.sepoints[k].pointPos;
                bool pointSelected = false;
                for (const SelectGeoId& sel : selectIds) {
                    if (sel.GeoId == geoId
                        && (sel.pointPos == PointPos::none || sel.pointPos == pos)) {
                        pointSelected = true;
                        break;
                    }
                }
                const bool pointPreSelected
                    = isPreSelected && preSelectGeoId.pointPos == pos;
                renderer->drawPoint(
                    mPlane.valueEigen(segment.sepoints[k].coord.x, segment.sepoints[k].coord.y),
                    pointSize + 1,
                    pointSelected ? selectColor
                                  : (pointPreSelected ? preselectColor
                                                      : m_drawOption.externalColor));
            }
        }
        // Point markers are drawn after the curves so they stay on top.
        for (int geoIndex = 0; geoIndex < static_cast<int>(mGeoList.size()); ++geoIndex) {
            if (mHiddenGeoIds.count(geoIndex)) {
                continue;
            }
            const bool isConstruction
                = mConstructionGeoIds.count(geoIndex) != 0
                || mGeoList[geoIndex]->getConstruction();
            auto& sePoints = mGeoSegment[mGeoList[geoIndex].get()].sepoints;
            for (int k = 0; k < static_cast<int>(sePoints.size()); ++k) {
                bool pointSelected = false;
                for (const auto& sel : selectIds) {
                    if (sel.GeoId == geoIndex && sel.pointPos == sePoints[k].pointPos) {
                        pointSelected = true;
                        break;
                    }
                }
                if (pointSelected) {
                    continue;  // already drawn in its selection colour above
                }
                renderer->drawPoint(
                    mPlane.valueEigen(sePoints[k].coord.x, sePoints[k].coord.y),
                    pointSize + 1,
                    isConstruction ? m_drawOption.constructionColor : pointColor
                );
            }
        }
        // Selected points are drawn last so they stay clearly on top.
        for (const auto& sel : selectIds) {
            if (sel.GeoId < 0 || sel.GeoId >= static_cast<int>(mGeoList.size())) {
                continue;
            }
            if (mHiddenGeoIds.count(sel.GeoId)) {
                continue;
            }
            auto& sePoints = mGeoSegment[mGeoList[sel.GeoId].get()].sepoints;
            for (const auto& sp : sePoints) {
                if (sp.pointPos == sel.pointPos) {
                    renderer->drawPoint(
                        mPlane.valueEigen(sp.coord.x, sp.coord.y),
                        pointSize + 2,
                        selectColor
                    );
                }
            }
        }
        renderer->popSize();
        drawConstraintLabels();
        drawTangentIcons();
        drawConstraintIcons();
    }
    // ------------------------------------------------------------------
// Dimension label overlay (P0)
// ------------------------------------------------------------------
    static void projectPointOnSegment2d(
        const Base::Vector2d& point,
        const Base::Vector2d& a,
        const Base::Vector2d& b,
        Base::Vector2d& proj
    )
    {
        Base::Vector2d ab(b.x - a.x, b.y - a.y);
        const double len2 = ab.x * ab.x + ab.y * ab.y;
        if (len2 < 1.0e-12) {
            proj = a;
            return;
        }
        double t = ((point.x - a.x) * ab.x + (point.y - a.y) * ab.y) / len2;
        t = std::max(0.0, std::min(1.0, t));
        proj.x = a.x + t * ab.x;
        proj.y = a.y + t * ab.y;
    }
    static bool intersectLines2d(
        const Base::Vector2d& p1,
        const Base::Vector2d& p2,
        const Base::Vector2d& p3,
        const Base::Vector2d& p4,
        Base::Vector2d& out
    )
    {
        const double dx1 = p2.x - p1.x;
        const double dy1 = p2.y - p1.y;
        const double dx2 = p4.x - p3.x;
        const double dy2 = p4.y - p3.y;
        const double det = dx1 * dy2 - dy1 * dx2;
        if (std::abs(det) < 1.0e-12) {
            return false;
        }
        const double t = ((p3.x - p1.x) * dy2 - (p3.y - p1.y) * dx2) / det;
        const double u = ((p3.x - p1.x) * dy1 - (p3.y - p1.y) * dx1) / det;
        // Accept slightly extended segments so shared-corner angles work even
        // when the polyline endpoints have tiny numeric gaps.
        if (t < -0.25 || t > 1.25 || u < -0.25 || u > 1.25) {
            return false;
        }
        out.x = p1.x + t * dx1;
        out.y = p1.y + t * dy1;
        return true;
    }
    void SketcherObj::updateConstraintLabelInteraction()
    {
        // While geometry itself is being dragged the label overlay must stay
        // out of the way; a label drag, however, is always continued here.
        if (isHaveActiveHandler || !isInEdit
            || (selectState == OperationGeo && m_labelDrag < 0)) {
            m_labelHover = -1;
            m_labelDrag = -1;
            return;
        }
        auto [mx, my] = m_sceneView->getInutState().GetMousePosition();
        if (m_labelDrag >= 0) {
            const Sketcher::Constraint* c = getConstraint(m_labelDrag);
            if (!c) {
                m_labelDrag = -1;
                return;
            }
            const bool straightDim = c->Type == Sketcher::ConstraintType::Distance
                || c->Type == Sketcher::ConstraintType::DistanceX
                || c->Type == Sketcher::ConstraintType::DistanceY;
            if (straightDim) {
                // Project the mouse onto the dimension shaft: the caption may
                // only slide along the segment, it never leaves the line.
                float trackAx = 0.0f, trackAy = 0.0f;
                float trackBx = 0.0f, trackBy = 0.0f;
                float gapX = 0.0f, gapY = 0.0f;
                if (computeStraightLabelTrack(
                    c, trackAx, trackAy, trackBx, trackBy, gapX, gapY
                )) {
                    const float dirX = trackBx - trackAx;
                    const float dirY = trackBy - trackAy;
                    const float len2 = dirX * dirX + dirY * dirY;
                    if (len2 > 1.0f) {
                        float t = ((static_cast<float>(mx) - trackAx) * dirX
                            + (static_cast<float>(my) - trackAy) * dirY)
                            / len2;
                        t = std::max(0.0f, std::min(1.0f, t));
                        m_labelManualParam[c] = t;
                    }
                    else {
                        m_labelManualParam[c] = 0.5;
                    }
                }
            }
            else if (c->Type == Sketcher::ConstraintType::Angle) {
                // The caption may only slide along the annotation arc:
                // convert the mouse to an angle around the arc centre and
                // store the position as a 0..1 parameter of the sweep.
                float cx = 0.0f, cy = 0.0f;
                float arcRadius = 0.0f, startDeg = 0.0f, sweepDeg = 0.0f;
                if (computeAngleLabelTrack(c, cx, cy, arcRadius, startDeg, sweepDeg)) {
                    const float mouseDeg = std::atan2(
                        static_cast<float>(my) - cy,
                        static_cast<float>(mx) - cx
                    ) * 180.0f / 3.14159265358979f;
                    float rel = mouseDeg - startDeg;
                    float t = 0.5f;
                    if (sweepDeg > 0.0f) {
                        if (rel < 0.0f) {
                            rel += 360.0f;
                        }
                        t = rel / sweepDeg;
                    }
                    else if (sweepDeg < 0.0f) {
                        if (rel > 0.0f) {
                            rel -= 360.0f;
                        }
                        t = rel / sweepDeg;
                    }
                    t = std::max(0.0f, std::min(1.0f, t));
                    m_labelManualParam[c] = t;
                }
            }
            else {
                Base::Vector2d offset = m_labelDragOffsetPx;
                offset.x += mx - m_labelDragPressPx.x;
                offset.y += my - m_labelDragPressPx.y;
                m_labelManualOffsetPx[c] = offset;
            }
            m_labelHover = m_labelDrag;
            return;
        }
        m_labelHover = pickConstraintLabelAt(static_cast<float>(mx), static_cast<float>(my));
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
        if (geo->isDerivedFrom<Part::GeomConic>()) {
            center = static_cast<const Part::GeomConic*>(geo)->getCenter();
        }
        else if (geo->is<Part::GeomArcOfConic>()) {
            center = static_cast<const Part::GeomArcOfConic*>(geo)->getCenter();
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
    bool SketcherObj::getConstraintMeasureEndpoints(
        const Sketcher::Constraint* constraint,
        Base::Vector2d& a,
        Base::Vector2d& b
    ) const
    {
        if (!constraint) {
            return false;
        }
        switch (constraint->Type) {
        case Sketcher::ConstraintType::DistanceX:
        case Sketcher::ConstraintType::DistanceY: {
            if (constraint->Second != Sketcher::GeoEnum::GeoUndef) {
                return getGeometryPointSketch(constraint->First, constraint->FirstPos, a)
                    && getGeometryPointSketch(constraint->Second, constraint->SecondPos, b);
            }
            if (constraint->FirstPos == PointPos::none) {
                return getGeometryPointSketch(constraint->First, PointPos::start, a)
                    && getGeometryPointSketch(constraint->First, PointPos::end, b);
            }
            // Coordinate constraint of a single point: DistanceX fixes the
            // x-coordinate, so the shaft spans from the point to the Y axis;
            // DistanceY fixes the y-coordinate and spans to the X axis.
            if (!getGeometryPointSketch(constraint->First, constraint->FirstPos, a)) {
                return false;
            }
            if (constraint->Type == Sketcher::ConstraintType::DistanceX) {
                b.x = 0.0;
                b.y = a.y;
            }
            else {
                b.x = a.x;
                b.y = 0.0;
            }
            return true;
        }
        case Sketcher::ConstraintType::Distance: {
            if (constraint->Second == Sketcher::GeoEnum::GeoUndef) {
                return getGeometryPointSketch(constraint->First, PointPos::start, a)
                    && getGeometryPointSketch(constraint->First, PointPos::end, b);
            }
            if (constraint->FirstPos != PointPos::none
                && constraint->SecondPos == PointPos::none) {
                Base::Vector2d la, lb;
                if (getGeometryPointSketch(constraint->First, constraint->FirstPos, a)
                    && getGeometryPointSketch(constraint->Second, PointPos::start, la)
                    && getGeometryPointSketch(constraint->Second, PointPos::end, lb)) {
                    projectPointOnSegment2d(a, la, lb, b);
                    return true;
                }
                return false;
            }
            if (constraint->FirstPos != PointPos::none
                && constraint->SecondPos != PointPos::none) {
                return getGeometryPointSketch(constraint->First, constraint->FirstPos, a)
                    && getGeometryPointSketch(constraint->Second, constraint->SecondPos, b);
            }
            return getGeometryCenterSketch(constraint->First, a)
                && getGeometryCenterSketch(constraint->Second, b);
        }
        default:
            return false;
        }
    }
    bool SketcherObj::computeStraightLabelTrack(
        const Sketcher::Constraint* constraint,
        float& trackAx,
        float& trackAy,
        float& trackBx,
        float& trackBy,
        float& gapX,
        float& gapY
    ) const
    {
        Base::Vector2d aSk, bSk;
        if (!getConstraintMeasureEndpoints(constraint, aSk, bSk)) {
            return false;
        }
        const auto worldOf = [this](const Base::Vector2d& sk) {
            return mPlane.origin + sk.x * mPlane.xAxis + sk.y * mPlane.yAxis;
            };
        const Eigen::Vector3f wa(
            static_cast<float>(worldOf(aSk).x),
            static_cast<float>(worldOf(aSk).y),
            static_cast<float>(worldOf(aSk).z)
        );
        const Eigen::Vector3f wb(
            static_cast<float>(worldOf(bSk).x),
            static_cast<float>(worldOf(bSk).y),
            static_cast<float>(worldOf(bSk).z)
        );
        const Eigen::Vector2f aS = renderer->worldToScreen(wa);
        const Eigen::Vector2f bS = renderer->worldToScreen(wb);
        float dx = bS.x() - aS.x();
        float dy = bS.y() - aS.y();
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 6.0f) {
            return false;
        }
        dx /= len;
        dy /= len;
        const float midX = (aS.x() + bS.x()) * 0.5f;
        const float midY = (aS.y() + bS.y()) * 0.5f;
        const bool isHorizDist = constraint->Type == Sketcher::ConstraintType::DistanceX;
        const bool isVertDist = constraint->Type == Sketcher::ConstraintType::DistanceY;
        const float shaftMargin = 16.0f;
        const float captionGap = 14.0f;
        if (isHorizDist || isVertDist) {
            // Horizontal/vertical distances always draw an axis-aligned
            // dimension shaft instead of a line parallel to the measured
            // segment: DistanceX stays horizontal above the points, DistanceY
            // stays vertical to the right of them.
            if (isHorizDist) {
                const float lineY = std::min(aS.y(), bS.y()) - shaftMargin;
                trackAx = aS.x();
                trackAy = lineY;
                trackBx = bS.x();
                trackBy = lineY;
                gapX = 0.0f;
                gapY = -captionGap;
            }
            else {
                // Vertical distance: shaft to the right of the rightmost point,
                // caption between the measured points and the shaft.
                const float lineX = std::max(aS.x(), bS.x()) + 26.0f;
                trackAx = lineX;
                trackAy = aS.y();
                trackBx = lineX;
                trackBy = bS.y();
                gapX = -captionGap;
                gapY = 0.0f;
            }
            return true;
        }

        // The default caption position (anchor + default pixel offset) tells
        // us which side of the measured geometry the dimension lives on.
        float defDx = 0.0f, defDy = 0.0f;
        defaultLabelOffsetPx(constraint, defDx, defDy);
        const float defaultX = midX + defDx;
        const float defaultY = midY + defDy;

        // Unit normal of the measured segment, oriented towards the default
        // caption position. The shaft is shifted a bit onto that side and the
        // caption keeps a fixed 14 px gap beyond the shaft.
        float nx = -dy;
        float ny = dx;
        float side = (defaultX - midX) * nx + (defaultY - midY) * ny;
        if (side < 0.0f) {
            nx = -nx;
            ny = -ny;
            side = -side;
        }
        float shaftDist = side - captionGap;
        // Keep a clear margin between the dimension shaft and the measured
        // geometry; the caption stays one fixed gap beyond the shaft.
        const float minShaftDist = 16.0f;
        if (shaftDist < minShaftDist) {
            shaftDist = minShaftDist;
        }
        trackAx = aS.x() + nx * shaftDist;
        trackAy = aS.y() + ny * shaftDist;
        trackBx = bS.x() + nx * shaftDist;
        trackBy = bS.y() + ny * shaftDist;
        gapX = nx * captionGap;
        gapY = ny * captionGap;
        return true;
    }
    bool SketcherObj::computeAngleLabelTrack(
        const Sketcher::Constraint* constraint,
        float& centerX,
        float& centerY,
        float& radiusPx,
        float& startDeg,
        float& sweepDeg
    ) const
    {
        if (!constraint || constraint->Type != Sketcher::ConstraintType::Angle) {
            return false;
        }
        Base::Vector2d vertex;
        Base::Vector2d dir1;
        Base::Vector2d dir2;
        bool valid = false;
        Base::Vector2d p1, p2;
        if (constraint->Second == Sketcher::GeoEnum::GeoUndef) {
            // Single line angle against the sketch x-axis: arc center is the
            // line start, sweep goes from the x-axis to the segment direction.
            if (getGeometryPointSketch(constraint->First, PointPos::start, p1)
                && getGeometryPointSketch(constraint->First, PointPos::end, p2)) {
                vertex = p1;
                dir1 = Base::Vector2d(1.0, 0.0);
                dir2 = p2 - p1;
                valid = (dir2.x != 0.0 || dir2.y != 0.0);
            }
        }
        else {
            // Angle between two lines: arc at the intersection, sweeping
            // between the far ends of both segments.
            Base::Vector2d a1, a2, b1, b2;
            if (getGeometryPointSketch(constraint->First, PointPos::start, a1)
                && getGeometryPointSketch(constraint->First, PointPos::end, a2)
                && getGeometryPointSketch(constraint->Second, PointPos::start, b1)
                && getGeometryPointSketch(constraint->Second, PointPos::end, b2)
                && intersectLines2d(a1, a2, b1, b2, vertex)) {
                const auto distSq = [](const Base::Vector2d& a, const Base::Vector2d& b) {
                    const double dx = a.x - b.x;
                    const double dy = a.y - b.y;
                    return dx * dx + dy * dy;
                    };
                const Base::Vector2d far1 = distSq(vertex, a1) > distSq(vertex, a2) ? a1 : a2;
                const Base::Vector2d far2 = distSq(vertex, b1) > distSq(vertex, b2) ? b1 : b2;
                dir1 = far1 - vertex;
                dir2 = far2 - vertex;
                valid = (dir1.x != 0.0 || dir1.y != 0.0) && (dir2.x != 0.0 || dir2.y != 0.0);
            }
        }
        if (!valid) {
            return false;
        }
        const auto worldOf = [this](const Base::Vector2d& sk) {
            return mPlane.origin + sk.x * mPlane.xAxis + sk.y * mPlane.yAxis;
            };
        const auto screenOf = [this, &worldOf](const Base::Vector2d& sk) {
            const Base::Vector3d w = worldOf(sk);
            return renderer->worldToScreen(
                Eigen::Vector3f(static_cast<float>(w.x), static_cast<float>(w.y), static_cast<float>(w.z))
            );
            };
        const Eigen::Vector2f vS = screenOf(vertex);
        const Eigen::Vector2f e1S = screenOf(Base::Vector2d(vertex.x + dir1.x, vertex.y + dir1.y));
        const Eigen::Vector2f e2S = screenOf(Base::Vector2d(vertex.x + dir2.x, vertex.y + dir2.y));
        const float pi = 3.14159265358979f;
        const float rawStart = std::atan2(e1S.y() - vS.y(), e1S.x() - vS.x()) * 180.0f / pi;
        const float rawEnd = std::atan2(e2S.y() - vS.y(), e2S.x() - vS.x()) * 180.0f / pi;
        float sweep = rawEnd - rawStart;
        while (sweep > 180.0f) {
            sweep -= 360.0f;
        }
        while (sweep < -180.0f) {
            sweep += 360.0f;
        }
        if (std::fabs(sweep) < 0.5f) {
            return false;
        }
        if (constraint->Second == Sketcher::GeoEnum::GeoUndef) {
            // Half of the segment length, matching the requested annotation.
            const Eigen::Vector2f p1S = screenOf(p1);
            const Eigen::Vector2f p2S = screenOf(p2);
            const float segLen = (p2S - p1S).norm();
            radiusPx = std::max(6.0f, segLen * 0.5f);
        }
        else {
            // Keep the two-line arc compact but inside both rays.
            const float ray1 = (e1S - vS).norm();
            const float ray2 = (e2S - vS).norm();
            const float minRay = std::min(ray1, ray2);
            radiusPx = std::max(10.0f, std::min(30.0f, minRay * 0.4f));
        }
        centerX = vS.x();
        centerY = vS.y();
        startDeg = rawStart;
        sweepDeg = sweep;
        return true;
    }
    Base::Vector2d SketcherObj::constraintLabelAnchor(const Sketcher::Constraint* c) const
    {
        Base::Vector2d p1, p2;
        if (!c) {
            return Base::Vector2d();
        }
        switch (c->Type) {
        case Sketcher::ConstraintType::DistanceX:
        case Sketcher::ConstraintType::DistanceY: {
            if (c->Second != Sketcher::GeoEnum::GeoUndef) {
                if (getGeometryPointSketch(c->First, c->FirstPos, p1)
                    && getGeometryPointSketch(c->Second, c->SecondPos, p2)) {
                    return (p1 + p2) * 0.5;
                }
            }
            else if (c->FirstPos != PointPos::none) {
                // coordinate of a single point
                if (getGeometryPointSketch(c->First, c->FirstPos, p1)) {
                    return p1;
                }
            }
            else if (getGeometryPointSketch(c->First, PointPos::start, p1)
                && getGeometryPointSketch(c->First, PointPos::end, p2)) {
                return (p1 + p2) * 0.5;
            }
            break;
        }
        case Sketcher::ConstraintType::Distance: {
            if (c->Second == Sketcher::GeoEnum::GeoUndef) {
                // length of a single edge: anchor on the middle of its curve
                const Part::Geometry* geo = resolveGeometry(c->First);
                if (geo) {
                    const auto it = mGeoSegment.find(const_cast<Part::Geometry*>(geo));
                    if (it != mGeoSegment.end() && !it->second.point.empty()) {
                        const Base::Vector3d mid = it->second.point[it->second.point.size() / 2];
                        return Base::Vector2d(mid.x, mid.y);
                    }
                }
            }
            else if (c->FirstPos != PointPos::none && c->SecondPos == PointPos::none) {
                // point -> line distance
                Base::Vector2d la, lb;
                if (getGeometryPointSketch(c->First, c->FirstPos, p1)
                    && getGeometryPointSketch(c->Second, PointPos::start, la)
                    && getGeometryPointSketch(c->Second, PointPos::end, lb)) {
                    Base::Vector2d proj;
                    projectPointOnSegment2d(p1, la, lb, proj);
                    return (p1 + proj) * 0.5;
                }
            }
            else if (c->FirstPos == PointPos::none && c->SecondPos == PointPos::none) {
                // curve -> curve distance
                if (getGeometryCenterSketch(c->First, p1) && getGeometryCenterSketch(c->Second, p2)) {
                    return (p1 + p2) * 0.5;
                }
            }
            else if (c->FirstPos != PointPos::none && c->SecondPos != PointPos::none) {
                // point -> point distance
                if (getGeometryPointSketch(c->First, c->FirstPos, p1)
                    && getGeometryPointSketch(c->Second, c->SecondPos, p2)) {
                    return (p1 + p2) * 0.5;
                }
            }
            break;
        }
        case Sketcher::ConstraintType::Radius:
        case Sketcher::ConstraintType::Diameter: {
            if (getGeometryCenterSketch(c->First, p1)) {
                return p1;
            }
            break;
        }
        case Sketcher::ConstraintType::Angle: {
            if (c->Second == Sketcher::GeoEnum::GeoUndef) {
                // single line against the sketch x-axis: anchor at the start
                // so the caption stays close to the angle arc
                if (getGeometryPointSketch(c->First, PointPos::start, p1)) {
                    return p1;
                }
            }
            else if (c->FirstPos != PointPos::none && c->SecondPos != PointPos::none) {
                if (getGeometryPointSketch(c->First, c->FirstPos, p1)
                    && getGeometryPointSketch(c->Second, c->SecondPos, p2)) {
                    return (p1 + p2) * 0.5;
                }
            }
            else if (c->FirstPos == PointPos::none && c->SecondPos == PointPos::none) {
                // angle between two lines: use their intersection when it is
                // near the segments, otherwise fall back to the middle point
                Base::Vector2d a1, a2, b1, b2;
                if (getGeometryPointSketch(c->First, PointPos::start, a1)
                    && getGeometryPointSketch(c->First, PointPos::end, a2)
                    && getGeometryPointSketch(c->Second, PointPos::start, b1)
                    && getGeometryPointSketch(c->Second, PointPos::end, b2)) {
                    Base::Vector2d inter;
                    if (intersectLines2d(a1, a2, b1, b2, inter)) {
                        return inter;
                    }
                    return ((a1 + a2) * 0.5 + (b1 + b2) * 0.5) * 0.5;
                }
            }
            break;
        }
        default:
            break;
        }
        // Fall back to the first geometry centre so the caption still has a
        // reasonable home even for less common element combinations.
        if (getGeometryCenterSketch(c->First, p1)) {
            return p1;
        }
        return Base::Vector2d();
    }
    std::string SketcherObj::constraintLabelText(const Sketcher::Constraint* c) const
    {
        if (!c) {
            return std::string();
        }
        char buf[96] = { 0 };
        const double value = c->getValue();
        switch (c->Type) {
        case Sketcher::ConstraintType::Radius:
            std::snprintf(buf, sizeof(buf), "R%.2f", value);
            break;
        case Sketcher::ConstraintType::Diameter:
            std::snprintf(buf, sizeof(buf), "D%.2f", value);
            break;
        case Sketcher::ConstraintType::Angle:
            std::snprintf(buf, sizeof(buf), "%.2f", value * 180.0 / 3.14159265358979323846);
            break;
        default:
            std::snprintf(buf, sizeof(buf), "%.2f", value);
            break;
        }
        return std::string(buf);
    }
    bool SketcherObj::constraintInError(int constrId) const
    {
        auto contains = [constrId](const std::vector<int>& list) {
            return std::find(list.begin(), list.end(), constrId) != list.end();
            };
        return contains(lastConflicting) || contains(lastRedundant)
            || contains(lastPartiallyRedundant) || contains(lastMalformedConstraints);
    }
    bool SketcherObj::computeConstraintLabel(
        int constrId,
        Base::Vector2d& anchorSketch,
        float& screenX,
        float& screenY
    ) const
    {
        if (constrId < 0 || constrId >= static_cast<int>(mConstraintList.size())) {
            return false;
        }
        const Sketcher::Constraint* c = mConstraintList[constrId];
        if (!c || !c->isVisible || !isDimensionLabelType(c->Type)) {
            return false;
        }
        if (c->First == Sketcher::GeoEnum::GeoUndef) {
            return false;  // no measured geometry to attach the caption to
        }
        const bool straightDim = c->Type == Sketcher::ConstraintType::Distance
            || c->Type == Sketcher::ConstraintType::DistanceX
            || c->Type == Sketcher::ConstraintType::DistanceY;
        if (straightDim) {
            float trackAx = 0.0f, trackAy = 0.0f;
            float trackBx = 0.0f, trackBy = 0.0f;
            float gapX = 0.0f, gapY = 0.0f;
            if (computeStraightLabelTrack(c, trackAx, trackAy, trackBx, trackBy, gapX, gapY)) {
                double t = 0.5;
                const auto paramIt = m_labelManualParam.find(c);
                if (paramIt != m_labelManualParam.end()) {
                    t = paramIt->second;
                }
                anchorSketch = constraintLabelAnchor(c);
                screenX = trackAx + (trackBx - trackAx) * static_cast<float>(t) + gapX;
                screenY = trackAy + (trackBy - trackAy) * static_cast<float>(t) + gapY;
                return true;
            }
        }
        if (c->Type == Sketcher::ConstraintType::Radius
            || c->Type == Sketcher::ConstraintType::Diameter) {
            // Radial captions stay outside the rim: direction comes from the
            // dragged label (or the default up-right corner), distance is the
            // screen radius plus a small gap.
            Base::Vector2d centerSk;
            const Part::Geometry* geo = resolveGeometry(c->First);
            double radius = -1.0;
            Base::Vector2d rimDirSk;
            if (getGeometryCenterSketch(c->First, centerSk) && geo
                && (geo->is<Part::GeomCircle>() || geo->is<Part::GeomArcOfCircle>())) {
                radius = geo->is<Part::GeomCircle>()
                    ? static_cast<const Part::GeomCircle*>(geo)->getRadius()
                    : static_cast<const Part::GeomArcOfCircle*>(geo)->getRadius();
                if (geo->is<Part::GeomArcOfCircle>()) {
                    const auto segIt = mGeoSegment.find(const_cast<Part::Geometry*>(geo));
                    if (segIt != mGeoSegment.end() && segIt->second.point.size() > 1) {
                        const Base::Vector3d mid = segIt->second.point[segIt->second.point.size() / 2];
                        const Base::Vector2d toMid(mid.x - centerSk.x, mid.y - centerSk.y);
                        const double dLen = std::sqrt(toMid.x * toMid.x + toMid.y * toMid.y);
                        if (dLen > 1.0e-9) {
                            rimDirSk = Base::Vector2d(toMid.x / dLen, toMid.y / dLen);
                        }
                    }
                }
                else {
                    rimDirSk = Base::Vector2d(0.70710678118, 0.70710678118);
                }
            }
            if (radius > 0.0 && (rimDirSk.x != 0.0 || rimDirSk.y != 0.0)) {
                const Base::Vector3d centerW = mPlane.origin + centerSk.x * mPlane.xAxis
                    + centerSk.y * mPlane.yAxis;
                const Base::Vector3d rimW = centerW + rimDirSk.x * mPlane.xAxis * radius
                    + rimDirSk.y * mPlane.yAxis * radius;
                const Eigen::Vector3f centerF(
                    static_cast<float>(centerW.x),
                    static_cast<float>(centerW.y),
                    static_cast<float>(centerW.z)
                );
                const Eigen::Vector3f rimF(
                    static_cast<float>(rimW.x),
                    static_cast<float>(rimW.y),
                    static_cast<float>(rimW.z)
                );
                const Eigen::Vector2f centerS = renderer->worldToScreen(centerF);
                const Eigen::Vector2f rimS = renderer->worldToScreen(rimF);
                float rx = rimS.x() - centerS.x();
                float ry = rimS.y() - centerS.y();
                const float rimDist = std::sqrt(rx * rx + ry * ry);
                float dirX = 0.70710678118f;
                float dirY = -0.70710678118f;  // up-right default
                const auto manualIt = m_labelManualOffsetPx.find(c);
                if (manualIt != m_labelManualOffsetPx.end()) {
                    const float dx = static_cast<float>(manualIt->second.x);
                    const float dy = static_cast<float>(manualIt->second.y);
                    const float dLen = std::sqrt(dx * dx + dy * dy);
                    if (dLen > 1.0f) {
                        dirX = dx / dLen;
                        dirY = dy / dLen;
                    }
                }
                if (rimDist > 1.0f) {
                    const float labelDist = rimDist + 18.0f;
                    anchorSketch = centerSk;
                    screenX = centerS.x() + dirX * labelDist;
                    screenY = centerS.y() + dirY * labelDist;
                    return true;
                }
            }
        }
        if (c->Type == Sketcher::ConstraintType::Angle) {
            // The caption lives on the annotation arc (slightly outside it so
            // the text box does not cover the arc itself).
            float cx = 0.0f, cy = 0.0f;
            float arcRadius = 0.0f, startDeg = 0.0f, sweepDeg = 0.0f;
            if (computeAngleLabelTrack(c, cx, cy, arcRadius, startDeg, sweepDeg)) {
                double t = 0.5;
                const auto paramIt = m_labelManualParam.find(c);
                if (paramIt != m_labelManualParam.end()) {
                    t = paramIt->second;
                }
                t = std::max(0.0, std::min(1.0, t));
                const float angleDeg = startDeg + sweepDeg * static_cast<float>(t);
                const float angleRad = angleDeg * 3.14159265358979f / 180.0f;
                const float captionRadius = arcRadius + 8.0f;
                anchorSketch = constraintLabelAnchor(c);
                screenX = cx + captionRadius * std::cos(angleRad);
                screenY = cy + captionRadius * std::sin(angleRad);
                return true;
            }
        }
        anchorSketch = constraintLabelAnchor(c);
        const Base::Vector3d world3 = mPlane.origin + anchorSketch.x * mPlane.xAxis
            + anchorSketch.y * mPlane.yAxis;
        const Eigen::Vector3f world(world3.x, world3.y, world3.z);
        const Eigen::Vector2f screen = renderer->worldToScreen(world);
        float dx = 0.0f, dy = 0.0f;
        const auto it = m_labelManualOffsetPx.find(c);
        if (it != m_labelManualOffsetPx.end()) {
            dx = static_cast<float>(it->second.x);
            dy = static_cast<float>(it->second.y);
        }
        else {
            defaultLabelOffsetPx(c, dx, dy);
        }
        screenX = screen.x() + dx;
        screenY = screen.y() + dy;
        return true;
    }
    int SketcherObj::pickConstraintLabelAt(float mouseX, float mouseY) const
    {
        if (!InEdit()) {
            return -1;
        }
        for (int i = static_cast<int>(mConstraintList.size()) - 1; i >= 0; --i) {
            const Sketcher::Constraint* c = mConstraintList[i];
            if (!c || !c->isVisible || !isDimensionLabelType(c->Type)) {
                continue;
            }
            Base::Vector2d anchor;
            float sx = 0.0f, sy = 0.0f;
            if (!computeConstraintLabel(i, anchor, sx, sy)) {
                continue;
            }
            const ImVec2 ts = ImGui::CalcTextSize(constraintLabelText(c).c_str());
            const float padX = 8.0f, padY = 5.0f;
            if (mouseX >= sx - ts.x * 0.5f - padX && mouseX <= sx + ts.x * 0.5f + padX
                && mouseY >= sy - ts.y * 0.5f - padY && mouseY <= sy + ts.y * 0.5f + padY) {
                return i;
            }
        }
        return -1;
    }
    void SketcherObj::drawConstraintLabels()
    {
        if (!InEdit() || mConstraintList.empty()) {
            return;
        }
        // Keep the hover state in sync with the mouse even when the cursor did
        // not move (e.g. the camera was zoomed with the wheel).
        if (m_labelDrag < 0) {
            if (!isHaveActiveHandler) {
                auto [mx, my] = m_sceneView->getInutState().GetMousePosition();
                m_labelHover = pickConstraintLabelAt(static_cast<float>(mx), static_cast<float>(my));
            }
            else {
                m_labelHover = -1;
            }
        }
        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        if (!drawList) {
            return;
        }
        const float thickness = 2.0f;
        const float pi = 3.14159265358979f;
        for (int i = 0; i < static_cast<int>(mConstraintList.size()); ++i) {
            const Sketcher::Constraint* c = mConstraintList[i];
            if (!c || !c->isVisible || !isDimensionLabelType(c->Type)) {
                continue;
            }
            Base::Vector2d anchor;
            float sx = 0.0f, sy = 0.0f;
            if (!computeConstraintLabel(i, anchor, sx, sy)) {
                continue;
            }
            const std::string text = constraintLabelText(c);
            const ImVec2 ts = ImGui::CalcTextSize(text.c_str());
            const bool isError = constraintInError(i);
            const bool hovered = (i == m_labelHover) || (i == m_labelDrag);
            const float padX = 6.0f, padY = 3.0f;
            const ImVec2 boxMin(sx - ts.x * 0.5f - padX, sy - ts.y * 0.5f - padY);
            const ImVec2 boxMax(sx + ts.x * 0.5f + padX, sy + ts.y * 0.5f + padY);
            const ImU32 arrowCol = isError ? IM_COL32(255, 110, 110, 255)
                : abgrToImU32(m_drawOption.constraintColor);
            auto screenOf = [&](const Base::Vector2d& sk) -> Eigen::Vector2f {
                return renderer->worldToScreen(mPlane.valueEigen(sk));
                };
            // Straight dimension shaft drawn with the shared double-arrow
            // primitive. The shaft position only depends on the measured
            // geometry, so dragging the caption never moves the shaft.
            const bool straightDim = c->Type == Sketcher::ConstraintType::Distance
                || c->Type == Sketcher::ConstraintType::DistanceX
                || c->Type == Sketcher::ConstraintType::DistanceY;
            if (straightDim) {
                float trackAx = 0.0f, trackAy = 0.0f;
                float trackBx = 0.0f, trackBy = 0.0f;
                float gapX = 0.0f, gapY = 0.0f;
                if (computeStraightLabelTrack(
                    c, trackAx, trackAy, trackBx, trackBy, gapX, gapY
                )) {
                    const float dx = trackBx - trackAx;
                    const float dy = trackBy - trackAy;
                    const float len = std::sqrt(dx * dx + dy * dy);
                    if (len > 4.0f) {
                        const float angleDeg = -std::atan2(dy, dx) * 180.0f / pi;
                        ImPlotCustom::drawDoubleArrow(
                            ImPlotCustom::Transform(trackAx, trackAy, angleDeg),
                            arrowCol,
                            len,
                            thickness,
                            nullptr
                        );
                    }
                }
            }
            else if (c->Type == Sketcher::ConstraintType::Radius
                || c->Type == Sketcher::ConstraintType::Diameter) {
                // Radial shaft: circles use a 45 degree direction towards the
                // caption, arcs point at the middle of the arc.
                Base::Vector2d centerSk;
                if (getGeometryCenterSketch(c->First, centerSk)) {
                    double radius = 0.0;
                    Base::Vector2d dirSk;
                    const Part::Geometry* geo = resolveGeometry(c->First);
                    if (geo && (geo->is<Part::GeomCircle>() || geo->is<Part::GeomArcOfCircle>())) {
                        radius = geo->is<Part::GeomCircle>()
                            ? static_cast<const Part::GeomCircle*>(geo)->getRadius()
                            : static_cast<const Part::GeomArcOfCircle*>(geo)->getRadius();
                        if (geo->is<Part::GeomArcOfCircle>()) {
                            const auto it = mGeoSegment.find(const_cast<Part::Geometry*>(geo));
                            if (it != mGeoSegment.end() && it->second.point.size() > 1) {
                                const Base::Vector3d mid = it->second.point[it->second.point.size() / 2];
                                const Base::Vector2d midSk(mid.x, mid.y);
                                const Base::Vector2d d = midSk - centerSk;
                                const double dLen = std::sqrt(d.x * d.x + d.y * d.y);
                                if (dLen > 1.0e-9) {
                                    dirSk = Base::Vector2d(d.x / dLen, d.y / dLen);
                                }
                            }
                        }
                        else {
                            dirSk = Base::Vector2d(0.70710678118, 0.70710678118);
                        }
                    }
                    if (radius > 0.0 && (dirSk.x != 0.0 || dirSk.y != 0.0)) {
                        const Eigen::Vector2f centerS = screenOf(centerSk);
                        const Eigen::Vector2f rimS = screenOf(
                            Base::Vector2d(centerSk.x + dirSk.x * radius, centerSk.y + dirSk.y * radius)
                        );
                        float rx = rimS.x() - centerS.x();
                        float ry = rimS.y() - centerS.y();
                        float rimDist = std::sqrt(rx * rx + ry * ry);
                        if (rimDist > 4.0f) {
                            // Point the radial shaft towards the caption when
                            // the user has dragged it to another side.
                            const float chipDx = sx - centerS.x();
                            const float chipDy = sy - centerS.y();
                            const float chipDist = std::sqrt(chipDx * chipDx + chipDy * chipDy);
                            if (chipDist > 12.0f) {
                                rx = chipDx / chipDist * rimDist;
                                ry = chipDy / chipDist * rimDist;
                            }
                            const float angleDeg = -std::atan2(ry, rx) * 180.0f / pi;
                            // Radius is drawn like a length dimension: a double
                            // arrow shaft that passes through the circle centre
                            // from one rim side to the other. Its direction is
                            // arbitrary and follows the dragged caption.
                            ImPlotCustom::drawDoubleArrow(
                                ImPlotCustom::Transform(
                                    centerS.x() - rx, centerS.y() - ry, angleDeg
                                ),
                                arrowCol,
                                rimDist * 2.0f,
                                thickness,
                                nullptr
                            );
                        }
                    }
                }
            }
            else if (c->Type == Sketcher::ConstraintType::Angle) {
                // Angle arc: center at the line start (single line) or at the
                // line intersection, radius half the segment length for the
                // single-line case. The caption is placed on this arc.
                float cx = 0.0f, cy = 0.0f;
                float arcRadius = 0.0f, startDeg = 0.0f, sweepDeg = 0.0f;
                if (computeAngleLabelTrack(c, cx, cy, arcRadius, startDeg, sweepDeg)) {
                    ImPlotCustom::drawDoubleArcArrow(
                        ImPlotCustom::Transform(cx, cy, startDeg),
                        arrowCol,
                        arcRadius,
                        sweepDeg,
                        thickness,
                        nullptr
                    );
                }
            }
            const ImU32 textCol = isError ? IM_COL32(255, 92, 92, 255)
                : abgrToImU32(m_drawOption.constraintColor);
            const ImU32 borderCol = isError ? IM_COL32(255, 120, 120, 220)
                : hovered ? IM_COL32(255, 255, 140, 255) : IM_COL32(255, 255, 255, 42);
            drawList->AddRectFilled(boxMin, boxMax, IM_COL32(24, 24, 30, 178), 4.0f);
            drawList->AddRect(boxMin, boxMax, borderCol, 4.0f);
            drawList->AddText(ImVec2(sx - ts.x * 0.5f, sy - ts.y * 0.5f), textCol, text.c_str());
        }
    }
    void SketcherObj::editConstraintValue(int constrId)
    {
        const Sketcher::Constraint* c = getConstraint(constrId);
        if (!c || !isDimensionLabelType(c->Type)) {
            return;
        }
        const bool isAngle = c->Type == Sketcher::ConstraintType::Angle;
        const double current = isAngle
            ? c->getValue() * 180.0 / 3.14159265358979323846
            : c->getValue();
        const double maxValue = isAngle ? 360.0 : 1.0e9;
        const std::string title = "Edit " + c->typeToString();
        bool ok = false;
        const double entered = QInputDialog::getDouble(
            nullptr,
            QString::fromStdString(title),
            QString::fromStdString(title),
            current,
            0.0,
            maxValue,
            3,
            &ok
        );
        if (!ok || std::abs(entered - current) < 1.0e-9) {
            return;
        }
        const double datum = isAngle ? entered * 3.14159265358979323846 / 180.0 : entered;
        const int err = setDatum(constrId, datum);
        if (err != 0) {
            CORE_ERROR("Constraint datum change failed, solver error code {}", err);
        }
    }
    bool SketcherObj::computeTangentIconAnchor(
        const Sketcher::Constraint* constraint,
        Base::Vector2d& anchorSketch,
        Base::Vector2d& dirSketch,
        Base::Vector2d& normalSketch
    ) const
    {
        if (!constraint || constraint->Type != Sketcher::ConstraintType::Tangent) {
            return false;
        }
        auto normalize2d = [](Base::Vector2d& v) {
            const double len = std::sqrt(v.x * v.x + v.y * v.y);
            if (len > 1.0e-9) {
                v.x /= len;
                v.y /= len;
                return true;
            }
            return false;
        };
        // Tangent direction of a geometry at a sketch point.
        auto tangentDirAt = [this, &normalize2d](int geoId, const Base::Vector2d& pt, Base::Vector2d& dir) {
            const Part::Geometry* geo = resolveGeometry(geoId);
            if (!geo) {
                return false;
            }
            if (geo->is<Part::GeomLineSegment>()) {
                Base::Vector2d s, e;
                if (getGeometryPointSketch(geoId, PointPos::start, s)
                    && getGeometryPointSketch(geoId, PointPos::end, e)) {
                    dir = e - s;
                    return normalize2d(dir);
                }
                return false;
            }
            Base::Vector2d center;
            double radius = 0.0;
            if (getCircleArcInfo(geo, center, radius)) {
                Base::Vector2d radialN(pt.x - center.x, pt.y - center.y);
                if (normalize2d(radialN)) {
                    dir = Base::Vector2d(-radialN.y, radialN.x);
                    return true;
                }
            }
            return false;
        };
        // Constraint stored with explicit point elements: anchor there.
        if (constraint->FirstPos != PointPos::none) {
            if (getGeometryPointSketch(constraint->First, constraint->FirstPos, anchorSketch)
                && tangentDirAt(constraint->First, anchorSketch, dirSketch)) {
                Base::Vector2d center;
                double radius = 0.0;
                if (getCircleArcInfo(resolveGeometry(constraint->First), center, radius)
                    || getCircleArcInfo(resolveGeometry(constraint->Second), center, radius)) {
                    normalSketch = Base::Vector2d(
                        anchorSketch.x - center.x,
                        anchorSketch.y - center.y
                    );
                    if (normalize2d(normalSketch)) {
                        return true;
                    }
                }
                normalSketch = Base::Vector2d(-dirSketch.y, dirSketch.x);
                return true;
            }
        }
        if (constraint->SecondPos != PointPos::none) {
            if (getGeometryPointSketch(constraint->Second, constraint->SecondPos, anchorSketch)
                && tangentDirAt(constraint->Second, anchorSketch, dirSketch)) {
                Base::Vector2d center;
                double radius = 0.0;
                if (getCircleArcInfo(resolveGeometry(constraint->First), center, radius)
                    || getCircleArcInfo(resolveGeometry(constraint->Second), center, radius)) {
                    normalSketch = Base::Vector2d(
                        anchorSketch.x - center.x,
                        anchorSketch.y - center.y
                    );
                    if (normalize2d(normalSketch)) {
                        return true;
                    }
                }
                normalSketch = Base::Vector2d(-dirSketch.y, dirSketch.x);
                return true;
            }
        }

        const Part::Geometry* g1 = resolveGeometry(constraint->First);
        const Part::Geometry* g2 = resolveGeometry(constraint->Second);
        if (!g1 || !g2) {
            return false;
        }

        // line + circle/arc: the tangency point is on the circle, on the
        // normal from the circle centre to the line.
        const Part::Geometry* lineGeo = nullptr;
        const Part::Geometry* circleGeo = nullptr;
        if (g1->is<Part::GeomLineSegment>() && isCircleArcGeometry(g2)) {
            lineGeo = g1;
            circleGeo = g2;
        }
        else if (g2->is<Part::GeomLineSegment>() && isCircleArcGeometry(g1)) {
            lineGeo = g2;
            circleGeo = g1;
        }
        if (lineGeo && circleGeo) {
            Base::Vector2d center;
            double radius = 0.0;
            if (getCircleArcInfo(circleGeo, center, radius)) {
                Base::Vector2d l0, l1;
                const bool firstIsLine = (g1 == lineGeo);
                const int lineId = firstIsLine ? constraint->First : constraint->Second;
                if (getGeometryPointSketch(lineId, PointPos::start, l0)
                    && getGeometryPointSketch(lineId, PointPos::end, l1)) {
                    Base::Vector2d u = l1 - l0;
                    if (normalize2d(u)) {
                        const double t = (center.x - l0.x) * u.x + (center.y - l0.y) * u.y;
                        const Base::Vector2d foot(l0.x + t * u.x, l0.y + t * u.y);
                        const Base::Vector2d radial(foot.x - center.x, foot.y - center.y);
                        Base::Vector2d radialN = radial;
                        if (normalize2d(radialN)) {
                            anchorSketch = Base::Vector2d(
                                center.x + radialN.x * radius,
                                center.y + radialN.y * radius
                            );
                            dirSketch = Base::Vector2d(-radialN.y, radialN.x);
                            normalSketch = radialN;
                            return true;
                        }
                    }
                }
            }
            return false;
        }

        // circle/arc + circle/arc: contact lies on the centre-to-centre line.
        Base::Vector2d c1, c2;
        double r1 = 0.0, r2 = 0.0;
        if (isCircleArcGeometry(g1) && isCircleArcGeometry(g2)
            && getCircleArcInfo(g1, c1, r1) && getCircleArcInfo(g2, c2, r2)) {
            const Base::Vector2d d(c2.x - c1.x, c2.y - c1.y);
            Base::Vector2d dn = d;
            if (normalize2d(dn)) {
                anchorSketch = Base::Vector2d(c1.x + dn.x * r1, c1.y + dn.y * r1);
                dirSketch = Base::Vector2d(-dn.y, dn.x);
                normalSketch = dn;
                return true;
            }
        }
        return false;
    }
    void SketcherObj::drawTangentIcons()
    {
        if (!InEdit()) {
            return;
        }
        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        if (!drawList) {
            return;
        }
        for (int i = 0; i < static_cast<int>(mConstraintList.size()); ++i) {
            const Sketcher::Constraint* c = mConstraintList[i];
            if (!c || !c->isVisible || c->Type != Sketcher::ConstraintType::Tangent) {
                continue;
            }
            Base::Vector2d anchorSketch;
            Base::Vector2d dirSketch;
            Base::Vector2d normalSketch;
            if (!computeTangentIconAnchor(c, anchorSketch, dirSketch, normalSketch)) {
                continue;
            }
            const bool isError = constraintInError(i);
            const ImU32 col = isError ? IM_COL32(255, 110, 110, 255)
                                      : abgrToImU32(m_drawOption.constraintColor);
            const auto screenOfSketch = [this](const Base::Vector2d& sk) {
                const Base::Vector3d w = mPlane.origin + sk.x * mPlane.xAxis
                    + sk.y * mPlane.yAxis;
                return renderer->worldToScreen(
                    Eigen::Vector3f(
                        static_cast<float>(w.x),
                        static_cast<float>(w.y),
                        static_cast<float>(w.z)
                    )
                );
            };
            const Eigen::Vector2f aS = screenOfSketch(anchorSketch);
            const Eigen::Vector2f dirS = screenOfSketch(
                Base::Vector2d(anchorSketch.x + dirSketch.x, anchorSketch.y + dirSketch.y)
            );
            const Eigen::Vector2f nrmS = screenOfSketch(
                Base::Vector2d(anchorSketch.x + normalSketch.x, anchorSketch.y + normalSketch.y)
            );
            float tx = dirS.x() - aS.x();
            float ty = dirS.y() - aS.y();
            float nx = nrmS.x() - aS.x();
            float ny = nrmS.y() - aS.y();
            const float tLen = std::sqrt(tx * tx + ty * ty);
            const float nLen = std::sqrt(nx * nx + ny * ny);
            if (tLen < 1.0e-3f || nLen < 1.0e-3f) {
                continue;
            }
            tx /= tLen;
            ty /= tLen;
            nx /= nLen;
            ny /= nLen;
            // FreeCAD style tangent icon: a small circle placed slightly
            // outside the tangency point with a tangent line touching its
            // outer side.
            const float circleR = 6.5f;
            const float iconOffset = 12.0f;
            const float lineHalf = 10.0f;
            const float cx = aS.x() + nx * iconOffset;
            const float cy = aS.y() + ny * iconOffset;
            const float lx0 = cx + nx * circleR - tx * lineHalf;
            const float ly0 = cy + ny * circleR - ty * lineHalf;
            const float lx1 = cx + nx * circleR + tx * lineHalf;
            const float ly1 = cy + ny * circleR + ty * lineHalf;
            drawList->AddCircle(ImVec2(cx, cy), circleR, col, 0, 2.0f);
            drawList->AddLine(ImVec2(lx0, ly0), ImVec2(lx1, ly1), col, 2.0f);
        }
    }
    void SketcherObj::drawConstraintIcons()
    {
        if (!InEdit()) {
            return;
        }
        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        if (!drawList) {
            return;
        }
        const auto anchorOf = [this](const Sketcher::Constraint* c, int idx, Base::Vector2d& out) {
            const int geoId = idx == 0 ? c->First : (idx == 1 ? c->Second : c->Third);
            const Sketcher::PointPos pos =
                idx == 0 ? c->FirstPos : (idx == 1 ? c->SecondPos : c->ThirdPos);
            if (geoId == Sketcher::GeoEnum::GeoUndef) {
                return false;
            }
            if (pos != Sketcher::PointPos::none) {
                return getGeometryPointSketch(geoId, pos, out);
            }
            return getGeometryCenterSketch(geoId, out);
        };
        const auto screenOf = [this](const Base::Vector2d& sk) {
            return renderer->worldToScreen(mPlane.valueEigen(sk));
        };

        for (int i = 0; i < static_cast<int>(mConstraintList.size()); ++i) {
            const Sketcher::Constraint* c = mConstraintList[i];
            if (!c || !c->isVisible || c->isDimensional()) {
                continue;
            }
            const bool isError = constraintInError(i);
            const ImU32 col = isError ? IM_COL32(255, 110, 110, 255)
                                      : abgrToImU32(m_drawOption.constraintColor);

            switch (c->Type) {
            case Sketcher::ConstraintType::Tangent:
                continue;  // handled by drawTangentIcons()
            case Sketcher::ConstraintType::Coincident: {
                Base::Vector2d a, b;
                if (anchorOf(c, 0, a)) {
                    Base::Vector2d p = a;
                    if (anchorOf(c, 1, b)) {
                        p = (a + b) * 0.5;
                    }
                    const Eigen::Vector2f s = screenOf(p);
                    // Offset the glyph away from the coincident point, then
                    // draw a circle with two dots inside.
                    const ImVec2 centre(s.x() + 16.0f, s.y() + 16.0f);
                    drawList->AddCircle(centre, 10.0f, col, 0, 1.8f);
                    drawList->AddCircleFilled(ImVec2(centre.x - 3.0f, centre.y), 2.5f, col);
                    drawList->AddCircleFilled(ImVec2(centre.x + 3.0f, centre.y), 2.5f, col);
                }
                break;
            }
            case Sketcher::ConstraintType::PointOnObject: {
                Base::Vector2d a;
                if (anchorOf(c, 0, a)) {
                    const Eigen::Vector2f s = screenOf(a);
                    // Screen Y grows downwards, so north-west = up-left:
                    // (-,-). Offset the arc centre away from the point so it
                    // does not cover it, then sweep 180..270 degrees.
                    const ImVec2 centre(s.x() - 5.0f, s.y() - 5.0f);
                    const float radius = 20.0f;
                    constexpr float kPi = 3.14159265358979f;
                    const int seg = 8;
                    ImVec2 arc[seg + 1];
                    for (int k = 0; k <= seg; ++k) {
                        const float deg = 180.0f + 90.0f * static_cast<float>(k) / seg;
                        const float a0 = deg * kPi / 180.0f;
                        arc[k] = ImVec2(
                            centre.x + radius * std::cos(a0),
                            centre.y + radius * std::sin(a0)
                        );
                    }
                    drawList->AddPolyline(arc, seg + 1, col, 0, 1.8f);
                    const float midDeg = 225.0f * kPi / 180.0f;
                    drawList->AddCircleFilled(
                        ImVec2(centre.x + radius * std::cos(midDeg),
                               centre.y + radius * std::sin(midDeg)),
                        3.0f,
                        col
                    );
                }
                break;
            }
            case Sketcher::ConstraintType::Horizontal:
            case Sketcher::ConstraintType::Vertical: {
                char indexText[16];
                std::snprintf(indexText, sizeof(indexText), "%d", i);
                const bool isHorizontal = c->Type == Sketcher::ConstraintType::Horizontal;
                const auto drawAlignmentIconAt = [&](const Base::Vector2d& anchor) {
                    const Eigen::Vector2f s = screenOf(anchor);
                    if (isHorizontal) {
                        // Horizontal alignment: a horizontal bar above the
                        // anchor, with the constraint index next to it.
                        const float offsetY = -22.0f;
                        const float halfLen = 16.0f;
                        drawList->AddLine(
                            ImVec2(s.x() - halfLen, s.y() + offsetY),
                            ImVec2(s.x() + halfLen, s.y() + offsetY),
                            col,
                            2.4f
                        );
                        drawList->AddText(
                            ImGui::GetFont(),
                            ImGui::GetFontSize() * 1.4f,
                            ImVec2(s.x() + halfLen + 5.0f, s.y() + offsetY - 9.0f),
                            col,
                            indexText
                        );
                    }
                    else {
                        // Vertical alignment: a vertical bar to the right of
                        // the anchor, with the constraint index next to it.
                        const float offsetX = 24.0f;
                        const float lineHalf = 18.0f;
                        drawList->AddLine(
                            ImVec2(s.x() + offsetX, s.y() - lineHalf),
                            ImVec2(s.x() + offsetX, s.y() + lineHalf),
                            col,
                            3.0f
                        );
                        drawList->AddText(
                            ImGui::GetFont(),
                            ImGui::GetFontSize() * 1.4f,
                            ImVec2(s.x() + offsetX + lineHalf + 5.0f, s.y() - 9.0f),
                            col,
                            indexText
                        );
                    }
                };
                Base::Vector2d a, b;
                if (anchorOf(c, 0, a)) {
                    drawAlignmentIconAt(a);
                }
                if (anchorOf(c, 1, b)) {
                    drawAlignmentIconAt(b);
                }
                break;
            }
            case Sketcher::ConstraintType::Equal: {
                Base::Vector2d a, b;
                char indexText[16];
                std::snprintf(indexText, sizeof(indexText), "%d", i);
                const auto drawEqualAt = [&](const Base::Vector2d& p) {
                    const Eigen::Vector2f s = screenOf(p);
                    drawList->AddLine(
                        ImVec2(s.x() - 9.0f, s.y() - 3.0f),
                        ImVec2(s.x() + 9.0f, s.y() - 3.0f),
                        col,
                        1.5f
                    );
                    drawList->AddLine(
                        ImVec2(s.x() - 9.0f, s.y() + 3.0f),
                        ImVec2(s.x() + 9.0f, s.y() + 3.0f),
                        col,
                        1.5f
                    );
                    drawList->AddText(
                        ImVec2(s.x() + 11.0f, s.y() - 8.0f),
                        col,
                        indexText
                    );
                };
                if (anchorOf(c, 0, a)) {
                    drawEqualAt(a);
                }
                if (anchorOf(c, 1, b)) {
                    drawEqualAt(b);
                }
                break;
            }
            case Sketcher::ConstraintType::Parallel: {
                Base::Vector2d a, b;
                if (anchorOf(c, 0, a) && anchorOf(c, 1, b)) {
                    // Two parallel lines share one constraint. Draw the "//"
                    // glyph next to every anchor, each tagged with the same
                    // constraint index, like Equal.
                    Base::Vector2d p1, p2;
                    Base::Vector2d dir;
                    if (getGeometryPointSketch(c->First, PointPos::start, p1)
                        && getGeometryPointSketch(c->First, PointPos::end, p2)) {
                        dir = p2 - p1;
                        const double dlen = std::sqrt(dir.x * dir.x + dir.y * dir.y);
                        if (dlen > 1.0e-6) {
                            dir.x /= dlen;
                            dir.y /= dlen;
                        }
                    }
                    char indexText[16];
                    std::snprintf(indexText, sizeof(indexText), "%d", i);
                    const auto drawParallelAt = [&](const Base::Vector2d& anchor) {
                        const Eigen::Vector2f s = screenOf(anchor);
                        Eigen::Vector2f dS;
                        if (dir.x != 0.0 || dir.y != 0.0) {
                            dS = screenOf(Base::Vector2d(anchor.x + dir.x, anchor.y + dir.y)) - s;
                        }
                        else {
                            dS = screenOf(b) - screenOf(a);
                        }
                        const float dl = dS.norm();
                        if (dl < 1.0e-3f) {
                            return;
                        }
                        dS /= dl;
                        const float nx = -dS.y();
                        const float ny = dS.x();
                        const float half = 12.0f;
                        const float sep = 5.0f;
                        drawList->AddLine(
                            ImVec2(s.x() + dS.x() * half + nx * sep,
                                   s.y() + dS.y() * half + ny * sep),
                            ImVec2(s.x() - dS.x() * half + nx * sep,
                                   s.y() - dS.y() * half + ny * sep),
                            col,
                            2.2f
                        );
                        drawList->AddLine(
                            ImVec2(s.x() + dS.x() * half - nx * sep,
                                   s.y() + dS.y() * half - ny * sep),
                            ImVec2(s.x() - dS.x() * half - nx * sep,
                                   s.y() - dS.y() * half - ny * sep),
                            col,
                            2.2f
                        );
                        drawList->AddText(
                            ImGui::GetFont(),
                            ImGui::GetFontSize() * 1.4f,
                            ImVec2(s.x() + dS.x() * (half + 7.0f) + nx * sep,
                                   s.y() + dS.y() * (half + 7.0f) + ny * sep - 7.0f),
                            col,
                            indexText
                        );
                    };
                    drawParallelAt(a);
                    drawParallelAt(b);
                }
                break;
            }
            case Sketcher::ConstraintType::Perpendicular: {
                Base::Vector2d a, b;
                char indexText[16];
                std::snprintf(indexText, sizeof(indexText), "%d", i);
                const auto drawPerpendicularAt = [&](const Base::Vector2d& anchor) {
                    const Eigen::Vector2f s = screenOf(anchor);
                    // A right-angle symbol per referenced object, like Equal.
                    const float leg = 16.0f;
                    const float thick = 2.4f;
                    drawList->AddLine(
                        ImVec2(s.x(), s.y()),
                        ImVec2(s.x() + leg, s.y()),
                        col,
                        thick
                    );
                    drawList->AddLine(
                        ImVec2(s.x() + leg, s.y()),
                        ImVec2(s.x() + leg, s.y() + leg),
                        col,
                        thick
                    );
                    drawList->AddText(
                        ImGui::GetFont(),
                        ImGui::GetFontSize() * 1.4f,
                        ImVec2(s.x() + leg + 5.0f, s.y() - 9.0f),
                        col,
                        indexText
                    );
                };
                if (anchorOf(c, 0, a)) {
                    drawPerpendicularAt(a);
                }
                if (anchorOf(c, 1, b)) {
                    drawPerpendicularAt(b);
                }
                break;
            }
            case Sketcher::ConstraintType::Symmetric: {
                Base::Vector2d a, b;
                if (anchorOf(c, 0, a) && anchorOf(c, 1, b)) {
                    const Base::Vector2d p = (a + b) * 0.5;
                    const Eigen::Vector2f s = screenOf(p);
                    drawList->AddLine(
                        ImVec2(s.x(), s.y() - 10.0f),
                        ImVec2(s.x(), s.y() + 10.0f),
                        col,
                        1.5f
                    );
                    drawList->AddCircleFilled(ImVec2(s.x() - 5.0f, s.y()), 2.0f, col);
                    drawList->AddCircleFilled(ImVec2(s.x() + 5.0f, s.y()), 2.0f, col);
                }
                break;
            }
            case Sketcher::ConstraintType::Block: {
                Base::Vector2d a;
                if (anchorOf(c, 0, a)) {
                    const Eigen::Vector2f s = screenOf(a);
                    drawList->AddRect(
                        ImVec2(s.x() - 8.0f, s.y() - 8.0f),
                        ImVec2(s.x() + 8.0f, s.y() + 8.0f),
                        col,
                        0.0f,
                        0,
                        1.5f
                    );
                }
                break;
            }
            default:
                break;
            }
        }
    }
}
