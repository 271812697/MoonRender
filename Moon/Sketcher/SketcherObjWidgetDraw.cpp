#include "Sketcher/SketcherObjWidget.h"
#include "Interactive/SketchPicking.h"
#include "Interactive/Widgets/DrawSketchHandler.h"
#include "Geometry.h"
#include "renderer/SceneView.h"
#include "Interactive/Im3DRenderer.h"
#include "Core/Global/ServiceLocator.h"
#include "core/log.h"
#include <Rendering/Overlay/Im2DRender.h>
#include "Sketcher/SketcheTool2D.h"
#include <QInputDialog>
#include <limits>
#include <cmath>
namespace MOON {
    // The sketch's annotations (dimension text, constraint arrows, constraint icons) are drawn
    // with Im2D, the overlay draw list that replaces ImGui's. See docs/Im2DRendering.md.
    namespace Im2D = ::Rendering::Overlay::Im2D;

    namespace {
        /** Annotation text size. The font atlas is baked at 24px, so this is crisp and the
         * exponent labels (1.4x) are only slightly magnified. */
        constexpr float kAnnotationFontSize = 26.0f;

        Im2D::DrawList& AnnotationDrawList()
        {
            return ::Rendering::Overlay::Im2DRender::Instance().GetDrawList();
        }

        Im2D::Font& AnnotationFont()
        {
            return ::Rendering::Overlay::Im2DRender::Instance().GetFont();
        }
    }
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
    /** The dimensions drawn as a line between two arrows, i.e. the ones whose ends can
     * be dragged (a radius or an angle has no second end to drag). */
    static bool isStraightDimension(Sketcher::ConstraintType type)
    {
        return type == Sketcher::ConstraintType::Distance
            || type == Sketcher::ConstraintType::DistanceX
            || type == Sketcher::ConstraintType::DistanceY;
    }
    static Im2D::ImU32 abgrToImU32(const Eigen::Vector4<uint8_t>& c)
    {
        return Im2D::ColorU32(c[3], c[2], c[1], c[0]);
    }
    // Draws a sketch-plane polyline with a screen-space dashed pattern (dash
    // and gap lengths are fixed in pixels, so the pattern keeps its size when
    // the view is zoomed). Construction geometry uses this style.
    static void drawDashedSketchPolyline(
        ImRenderer* renderer,
        const SketcherPlane2D& plane,
        const std::vector<Base::Vector3d>& points,
        float dashPx = 8.0f,
        float gapPx = 6.0f
    )
    {
        if (!renderer || points.size() < 2) {
            return;
        }
        // The dash pattern is a renderer feature now (ImRenderer::drawDashedPolyline): it keeps
        // the points in world space, so a construction line goes through the same pipeline as a
        // solid one, and only the dash rhythm is measured in screen pixels.
        std::vector<Eigen::Vector3f> world;
        world.reserve(points.size());
        for (const Base::Vector3d& point : points) {
            world.push_back(plane.valueEigen(point.x, point.y));
        }
        renderer->drawDashedPolyline(world, dashPx, gapPx);
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
    namespace
    {
        /** Where the sketch backdrop - the grid and the axes - sits in depth.
         *
         * The backdrop is part of the scene and is drawn in the renderer's
         * depth-tested batch (see drawBackground), so that a solid standing in front of
         * the sketch plane hides it and one standing behind it is covered by it. That
         * costs the freedom the overlay batch gave: the near and the far plane cut a
         * depth-tested backdrop like they cut anything else, and a sketch plane seen at
         * a glancing angle runs along the view direction for thousands of units, so its
         * far end is beyond the far plane and used to leave a hole across the view.
         *
         * The way out is the same property that made the old flattening work: under an
         * orthographic camera, moving a point along the view direction does not move it
         * on screen, only its depth. So the part of a segment that is beyond the far
         * plane is not dropped - it is slid onto the far plane, which shows it in the
         * same place while giving it the farthest depth there is. Nothing else is drawn
         * out there (it would be cut as well), so every occlusion decision comes out as
         * it would with the segment's own depth - and in the part that is inside the
         * range, which is where all the scene geometry is, its own depth is used.
         *
         * The offset towards the camera is there because a face the sketch was made on
         * is coplanar with the sketch plane: at exactly equal depth the two would trade
         * pixels, since the face comes from a triangulation and lands a hair off the
         * plane. A fraction of the depth range is far smaller than anything the user can
         * draw and is still enough to settle the comparison.
         */
        struct BackdropDepth
        {
            BackdropDepth() = default;
            BackdropDepth(
                const Eigen::Vector3f& p_eye,
                const Eigen::Vector3f& p_forward,
                float p_nearPlane,
                float p_farPlane
            ) :
                eye(p_eye),
                forward(p_forward),
                nearPlane(p_nearPlane),
                farPlane(p_farPlane),
                usable(true)
            {
                const float range = p_farPlane - p_nearPlane;
                // A hair inside the real clip planes: a vertex exactly on one is a coin
                // flip for the clipper, and being cut is what this is here to avoid.
                lowBound = p_nearPlane + range * 1.0e-3f;
                highBound = p_farPlane - range * 1.0e-3f;
                bias = range * 1.0e-5f;
            }

            /** Distance of a world point along the view direction. */
            float depthOf(const Eigen::Vector3f& p_point) const
            {
                return (p_point - eye).dot(forward);
            }

            /** The same point slid to another depth, i.e. moved along the view direction
             * only: under an orthographic camera it stays where it is on screen. */
            Eigen::Vector3f atDepth(const Eigen::Vector3f& p_point, float p_depth) const
            {
                return p_point + forward * (p_depth - depthOf(p_point));
            }

            /** A backdrop point, nudged off the plane towards the camera (so that a
             * coplanar face loses the comparison instead of speckling with it) and kept
             * inside the drawable depth range. */
            Eigen::Vector3f place(const Eigen::Vector3f& p_point) const
            {
                if (!usable) {
                    return p_point;
                }
                return atDepth(p_point - forward * bias, inRange(depthOf(p_point) - bias));
            }

            float inRange(float p_depth) const
            {
                return std::min(std::max(p_depth, lowBound), highBound);
            }

            Eigen::Vector3f eye = Eigen::Vector3f::Zero();
            Eigen::Vector3f forward = Eigen::Vector3f(0.0f, 0.0f, -1.0f);
            float nearPlane = 0.0f;
            float farPlane = 0.0f;
            /** The depths the backdrop is kept between: a hair inside the clip planes. */
            float lowBound = 0.0f;
            float highBound = 0.0f;
            float bias = 0.0f;
            /** False when the camera cannot describe a depth range: the backdrop is
             * then drawn where it is and left to the renderer's own clipping. */
            bool usable = false;
        };

        /** One backdrop segment, split into the pieces it has to be drawn as: the
         * segment itself, which the clip planes narrow to its own depth range for free,
         * plus the parts that run past the near or the far plane, painted flat on that
         * plane. Up to three pieces, in drawing order. */
        struct BackdropSegment
        {
            struct Piece
            {
                Eigen::Vector3f a;
                Eigen::Vector3f b;
            };
            Piece pieces[3];
            int count = 0;
        };
    }

    /** @return the depth range the backdrop has to stay inside. A camera that cannot
     * describe one - none at all, not orthographic (the sketch is edited with an
     * orthographic camera, so this is the odd case), or a depth range that is not a
     * range - gets the unusable one, which clips and nudges nothing. */
    static BackdropDepth MakeBackdropDepth(const Rendering::Entities::Camera* p_camera)
    {
        if (p_camera == nullptr
            || p_camera->GetProjectionMode() != ::Rendering::Settings::EProjectionMode::ORTHOGRAPHIC) {
            return {};
        }
        // (not "near" / "far": the Windows SDK still has those as macros)
        const float nearPlane = p_camera->GetNear();
        const float farPlane = p_camera->GetFar();
        if (!std::isfinite(nearPlane) || !std::isfinite(farPlane) || farPlane <= nearPlane) {
            return {};
        }
        const Maths::FVector3 eye = p_camera->GetPosition();
        const Maths::FVector3 look = p_camera->GetRotation() * Maths::FVector3::Forward;
        return BackdropDepth(
            Eigen::Vector3f(eye.x, eye.y, eye.z),
            Eigen::Vector3f(look.x, look.y, look.z),
            nearPlane,
            farPlane);
    }

    /** Splits one backdrop segment into the pieces described by BackdropSegment: the
     * segment at its own depth (the clip planes cut that to the range by themselves) and
     * the overhangs, painted flat on the plane they run past so the backdrop keeps
     * filling the view instead of stopping at the far plane. */
    static BackdropSegment splitBackdropSegment(
        const BackdropDepth& p_depth,
        const Eigen::Vector3f& p_a,
        const Eigen::Vector3f& p_b
    )
    {
        BackdropSegment out;
        const auto add = [&out](const Eigen::Vector3f& p_a, const Eigen::Vector3f& p_b) {
            out.pieces[out.count].a = p_a;
            out.pieces[out.count].b = p_b;
            ++out.count;
            };
        add(p_depth.place(p_a), p_depth.place(p_b));
        if (!p_depth.usable) {
            return out;
        }
        const float da = p_depth.depthOf(p_a);
        const float db = p_depth.depthOf(p_b);
        const float d = db - da;
        if (std::abs(d) < 1.0e-9f) {
            // The segment runs across the view at one depth: either it is in range and
            // the plain piece is that, or it is entirely on one side - in which case it
            // was just cut away and is painted on that plane here. (place() above already
            // slid it there, so counting it once is enough.)
            return out;
        }
        // The two parameters where the segment's depth crosses a bound. Sorted, the
        // interval between them is the part inside the range.
        const float s0 = (p_depth.lowBound - da) / d;
        const float s1 = (p_depth.highBound - da) / d;
        const float firstCrossing = std::min(s0, s1);
        const float lastCrossing = std::max(s0, s1);
        // Which bound the segment leaves through first, i.e. what the overhang at the
        // start of the segment has to be painted on.
        const float lowSide = (d > 0.0f) ? p_depth.lowBound : p_depth.highBound;
        const float highSide = (d > 0.0f) ? p_depth.highBound : p_depth.lowBound;
        const Eigen::Vector3f span = p_b - p_a;
        const auto at = [&](float p_s) {
            return p_a + span * p_s;
            };
        if (firstCrossing > 0.0f) {
            const float end = std::min(firstCrossing, 1.0f);
            add(
                p_depth.place(p_depth.atDepth(at(0.0f), lowSide)),
                p_depth.place(p_depth.atDepth(at(end), lowSide)));
        }
        if (lastCrossing < 1.0f) {
            const float start = std::max(lastCrossing, 0.0f);
            add(
                p_depth.place(p_depth.atDepth(at(start), highSide)),
                p_depth.place(p_depth.atDepth(at(1.0f), highSide)));
        }
        return out;
    }

    void SketcherObjWidget::fitCamera()
    {
        auto& view = GetService(Editor::Panels::SceneView);
        //view.GetCameraController().EnableRotate(false);
        view.GetCamera()->SetSize(100);
        view.GetCamera()->SetProjectionMode(Rendering::Settings::EProjectionMode::ORTHOGRAPHIC);
        // Half the far plane is the distance that puts the sketch in the middle of the
        // depth range. A perspective camera carries no far plane (it is infinite), and
        // this runs in the very frame the view was switched, before the next
        // UpdateDepthRange has sized the planes for an orthographic one - so the
        // distance needs a fallback. Which distance it is barely matters: an
        // orthographic view shows the plane the same from anywhere, it only has to be
        // finite and in front of the near plane the next frame computes.
        // (not named "far": the Windows SDK still has that as a macro)
        const float farPlane = view.GetCamera()->GetFar();
        const float pos = std::isfinite(farPlane) && farPlane > 0.0f ? farPlane * 0.5f : 1000.0f;
        Maths::FVector3 normal(m_sketch->plane().normal.x, m_sketch->plane().normal.y, m_sketch->plane().normal.z);
        Maths::FVector3 up(m_sketch->plane().yAxis.x, m_sketch->plane().yAxis.y, m_sketch->plane().yAxis.z);
        Maths::FQuaternion quat = Maths::FQuaternion::LookAt(-normal, up);
        view.GetCameraController().MoveToPose(Maths::FVector3(m_sketch->plane().origin.x, m_sketch->plane().origin.y, m_sketch->plane().origin.z) + normal * pos, quat);
        // m_sketch->getplaneTransform() is the plane's own, set by setPlane(): this only
        // puts the camera where it looks at that plane.
    }
    void SketcherObjWidget::beginEdit()
    {
        isInEdit = true;
        setActive(true);
        fitCamera();
        // Whatever the sketch had done before this session, it has been seen.
        m_seenPlaneRevision = m_sketch->planeRevision();
        m_seenConstraintRevision = m_sketch->constraintRevision();
    }
    /** The sketch is done: commit its shape, then leave the edit session. The task
    * dialog calls this instead of the sketch's makeDone(), so ending a session never
    * travels from the data layer back up to the widget. */
    void SketcherObjWidget::finishEdit()
    {
        m_sketch->makeDone();
        leaveEdit();
    }
    /** The sketch data does not call this widget - it counts its changes. Reading
    * those counters here keeps the dependency one way. */
    void SketcherObjWidget::syncWithSketch()
    {
        if (m_sketch == nullptr) {
            return;
        }
        if (m_seenPlaneRevision != m_sketch->planeRevision()) {
            m_seenPlaneRevision = m_sketch->planeRevision();
            // The sketch lies somewhere else now: look at it.
            fitCamera();
        }
        if (m_seenConstraintRevision != m_sketch->constraintRevision()) {
            m_seenConstraintRevision = m_sketch->constraintRevision();
            pruneConstraintLayout();
        }
        applyPendingAnnotationDrops();
        }
    /** Puts the annotations a dimension tool dropped where the user pointed: the tool
    * left the point in the sketch data (it owns no layout), and turning it into the
    * layout this widget keeps is what this does. */
    void SketcherObjWidget::applyPendingAnnotationDrops()
    {
        const auto& drops = m_sketch->annotationDropPoints();
        if (drops.empty()) {
            return;
        }
        // Copied first: placing one clears its entry.
        std::vector<std::pair<const Sketcher::Constraint*, Base::Vector2d>> pending(
            drops.begin(), drops.end());
        for (const auto& entry : pending) {
            const Sketcher::Constraint* c = entry.first;
            int constrId = -1;
            for (int i = 0; i < m_sketch->getConstraintCount(); ++i) {
                if (m_sketch->getConstraint(i) == c) {
                    constrId = i;
                    break;
                }
            }
            if (constrId >= 0) {
                const Eigen::Vector3f world
                    = m_sketch->plane().valueEigen(entry.second.x, entry.second.y);
                const Eigen::Vector2f screen = renderer->worldToScreen(world);
                placeDimensionAnnotation(constrId, screen.x(), screen.y());
            }
            m_sketch->clearAnnotationDropPoint(c);
        }
    }
    /** Drops the annotation layout of constraints that are gone. The maps are keyed
    * by the constraint itself, so an entry a dead constraint left behind would be
    * inherited by a later one allocated at the same address. */
    void SketcherObjWidget::pruneConstraintLayout()
    {
        const auto alive = [this](const Sketcher::Constraint* p_constraint) {
            for (int i = 0; i < m_sketch->getConstraintCount(); ++i) {
                if (m_sketch->getConstraint(i) == p_constraint) {
                    return true;
                }
            }
            return false;
        };
        const auto prune = [&alive](auto& p_map) {
            for (auto it = p_map.begin(); it != p_map.end();) {
                if (alive(it->first)) {
                    ++it;
                }
                else {
                    it = p_map.erase(it);
                }
            }
        };
        prune(m_labelManualOffsetSketch);
        prune(m_labelManualParam);
        prune(m_straightDimOffsetSketch);
        prune(m_angleLabelRadiusSketch);
        // The hover/drag state is held by index, and the indices have moved.
        m_labelHover = -1;
        m_labelDrag = -1;
        m_labelHoverHandle = LabelHandle::Caption;
        m_labelDragHandle = LabelHandle::Caption;
    }
    /** Leaves the edit session: the camera goes back to the user and the drawing
     * tools are switched off. The sketch data is not touched - SketcherObj::makeDone()
     * does that part before calling this (see there). */
    void SketcherObjWidget::leaveEdit()
    {
        isInEdit = false;
        auto& view = GetService(Editor::Panels::SceneView);
        view.GetCameraController().EnableRotate(true);
    }
    void SketcherObjWidget::onUpdate()
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
            if (m_sketch->getPreSelectGeoId().GeoId < 0) {
            m_sketch->clearPreselect();
            }
            }
            syncWithSketch();
            draw();
            }
    void SketcherObjWidget::drawBackground()
    {
        if (!InEdit()) return;

        // Adaptive background grid: the major cells carry the stronger tone, the
        // minor subdivisions inside them the weaker one. The spacing snaps to a
        // 1 / 2 x 10^n step (see SketchPicking::gridView) so the on-screen density
        // stays roughly constant while zooming, and so that the cell sides run
        // 1, 0.2, 0.1, 0.02, 0.01 - round decimal values - instead of the
        // 1, 0.5, 0.2, 0.1 the usual 1/2/5 ladder gives.
        //
        // Everything here is worked out in the (u, v) of the sketch plane, because
        // that is the space the grid lives in - and that is exactly what the camera
        // angle changes. An orthographic camera projects along its own view direction,
        // so the part of the plane it shows is not a rectangle in (u, v): it is the
        // quad where both view-space coordinates of a plane point stay inside the
        // viewport. Reading the visible range off the viewport corners as an
        // axis-aligned rectangle only holds face on - tilted, the true quad sticks out
        // of that rectangle and the grid is left with corners missing.
        if (m_sketch->isDrawGrid()) {
            const Eigen::Vector4<uint8_t>& minorColor = m_drawOption.gridMinorColor;
            const Eigen::Vector4<uint8_t>& majorColor = m_drawOption.gridMajorColor;
            const float gridLineWidth = m_drawOption.gridLineWidth;
            if (m_sceneView && m_sceneView->GetCamera()) {
                // The backdrop belongs to the scene and not to an overlay: it goes into
                // the depth-tested batch, so a solid standing in front of the sketch
                // plane hides it and one standing behind it is covered by it. The
                // overlay batch it used to be drawn in has no depth test at all, which
                // is exactly why the grid used to paint itself over the whole view.
                const BackdropDepth depth = MakeBackdropDepth(m_sceneView->GetCamera());
                renderer->pushEnableSorting(true);
                // One grid segment. splitBackdropSegment() hands back the pieces it has
                // to be drawn as: itself (its own depth, which is what the occlusion
                // needs) plus, where it runs past a clip plane, the overhang painted on
                // that plane, so the grid still reaches the edge of the view.
                const auto drawSegment = [&](
                    const Eigen::Vector3f& p_a,
                    const Eigen::Vector3f& p_b,
                    const Eigen::Vector4<uint8_t>& p_color) {
                    const BackdropSegment segment = splitBackdropSegment(depth, p_a, p_b);
                    for (int i = 0; i < segment.count; ++i) {
                        renderer->drawLine(
                            segment.pieces[i].a,
                            segment.pieces[i].b,
                            gridLineWidth,
                            p_color);
                    }
                };
                // What a camera the analysis below cannot describe gets: one whose view
                // direction lies in the sketch plane.
                const auto drawFixedGrid = [&]() {
                    const float extent = 100.0f;
                    for (int k = -10; k <= 10; ++k) {
                        const float g = k * 10.0f;
                        const auto& col = (k % 5) == 0 ? majorColor : minorColor;
                        drawSegment(
                            m_sketch->plane().valueEigen(g, -extent),
                            m_sketch->plane().valueEigen(g, extent),
                            col);
                        drawSegment(
                            m_sketch->plane().valueEigen(-extent, g),
                            m_sketch->plane().valueEigen(extent, g),
                            col);
                    }
                };

                SketchPicking::GridView grid;
                if (!SketchPicking::gridView(*m_sketch, *m_sceneView, grid)) {
                    drawFixedGrid();
                    renderer->popEnableSorting();
                    return;
                }

                // The visible quad: its four corners are the crossings of the two pairs
                // of viewport borders, i.e. of
                //     oX + u * uX + v * vX = +-hx      (left / right)
                //     oY + u * uY + v * vY = +-hy      (bottom / top).
                // The pairs meet in a point unless the view direction lies in the plane
                // (det == 0), which the fallback above has already taken care of.
                const float det = grid.uX * grid.vY - grid.vX * grid.uY;
                float uMin = std::numeric_limits<float>::max();
                float uMax = -std::numeric_limits<float>::max();
                float vMin = uMin;
                float vMax = uMax;
                for (int sx = 0; sx < 2; ++sx) {
                    const float cx = (sx == 0 ? grid.hx : -grid.hx) - grid.oX;
                    for (int sy = 0; sy < 2; ++sy) {
                        const float cy = (sy == 0 ? grid.hy : -grid.hy) - grid.oY;
                        const float u = (cx * grid.vY - grid.vX * cy) / det;
                        const float v = (grid.uX * cy - cx * grid.uY) / det;
                        uMin = std::min(uMin, u);
                        uMax = std::max(uMax, u);
                        vMin = std::min(vMin, v);
                        vMax = std::max(vMax, v);
                    }
                }

                const float step = grid.step;

                // How the two families crowd as the view turns. Lines of constant u are
                // spaced along the u axis, and the plane's axes are sheared by the
                // projection, so the *perpendicular* distance between two of them is
                //     step * scaleU * sin(angle between the projected axes)
                // in view space; the same for the other family with scaleV. The step is
                // sized by the larger of the two scales (see SketchPicking::gridView), so
                // the family across the foreshortened axis is the one that can come out
                // denser than the target - and when it gets below a few pixels it turns
                // into a grey band. Fading it by its own spacing keeps the rotation smooth
                // without letting that happen.
                const float scaleU = std::sqrt(grid.uX * grid.uX + grid.uY * grid.uY);
                const float scaleV = std::sqrt(grid.vX * grid.vX + grid.vY * grid.vY);
                const float sinTheta = (scaleU > 1.0e-6f && scaleV > 1.0e-6f)
                    ? std::abs(det) / (scaleU * scaleV)
                    : 0.0f;
                const float viewportHeight = static_cast<float>(
                    m_sceneView->GetRenderer().GetFrameDescriptor().renderHeight);
                const float pixelsPerViewUnit = viewportHeight / (2.0f * grid.hy);
                constexpr float kGridFadeStartPx = 8.0f;
                constexpr float kGridFadeEndPx = 4.0f;
                const auto familyFade = [&](float p_scaleAlongAxis) {
                    const float spacingPx
                        = grid.step * p_scaleAlongAxis * sinTheta * pixelsPerViewUnit;
                    return std::clamp(
                        (spacingPx - kGridFadeEndPx) / (kGridFadeStartPx - kGridFadeEndPx),
                        0.0f, 1.0f);
                };

                // Narrows [p_lo, p_hi] down to the values of the free parameter that keep
                // p_offset + p_coeff * t inside [-p_limit, p_limit], i.e. to the part of
                // the line that is inside the viewport.
                // @return false when no value of t is.
                const auto narrowSpan = [](
                    float p_offset, float p_coeff, float p_limit, float& p_lo, float& p_hi) {
                    if (std::abs(p_coeff) < 1.0e-6f) {
                        // The line runs parallel to this pair of borders: either all of it
                        // is between them or none of it is.
                        return std::abs(p_offset) <= p_limit;
                    }
                    const float t1 = (-p_limit - p_offset) / p_coeff;
                    const float t2 = (p_limit - p_offset) / p_coeff;
                    p_lo = std::max(p_lo, std::min(t1, t2));
                    p_hi = std::min(p_hi, std::max(t1, t2));
                    return p_lo <= p_hi;
                };
                // A grazing view stretches the quad without bound; the cap keeps the loop
                // below finite by dropping the lines farthest out, which are far off
                // screen by then anyway.
                constexpr int kMaxLinesPerFamily = 2000;
                // Lines of constant u (they run along v), then lines of constant v.
                const auto drawFamily = [&](bool p_constantU) {
                    // The family across the foreshortened axis crowds first; below a few
                    // pixels of spacing it is just a grey band, so it fades out.
                    const float fade = familyFade(p_constantU ? scaleU : scaleV);
                    if (fade <= 0.0f) {
                        return;
                    }
                    const float fixedX = p_constantU ? grid.uX : grid.vX;
                    const float fixedY = p_constantU ? grid.uY : grid.vY;
                    const float freeX = p_constantU ? grid.vX : grid.uX;
                    const float freeY = p_constantU ? grid.vY : grid.uY;
                    const float fixedMin = p_constantU ? uMin : vMin;
                    const float fixedMax = p_constantU ? uMax : vMax;
                    const float freeMin = p_constantU ? vMin : uMin;
                    const float freeMax = p_constantU ? vMax : uMax;
                    int first = static_cast<int>(std::ceil(fixedMin / step));
                    int last = static_cast<int>(std::floor(fixedMax / step));
                    if (last - first > kMaxLinesPerFamily) {
                        const int middle = first + (last - first) / 2;
                        first = middle - kMaxLinesPerFamily / 2;
                        last = first + kMaxLinesPerFamily;
                    }
                    for (int i = first; i <= last; ++i) {
                        const float g = i * step;
                        float lo = freeMin;
                        float hi = freeMax;
                        if (!narrowSpan(grid.oX + g * fixedX, freeX, grid.hx, lo, hi)
                            || !narrowSpan(grid.oY + g * fixedY, freeY, grid.hy, lo, hi)) {
                            continue;
                        }
                        Eigen::Vector4<uint8_t> col = (i % 5) == 0 ? majorColor : minorColor;
                        col[0] = static_cast<uint8_t>(
                            static_cast<float>(col[0]) * fade + 0.5f);
                        const Eigen::Vector3f rawA = p_constantU
                            ? m_sketch->plane().valueEigen(g, lo)
                            : m_sketch->plane().valueEigen(lo, g);
                        const Eigen::Vector3f rawB = p_constantU
                            ? m_sketch->plane().valueEigen(g, hi)
                            : m_sketch->plane().valueEigen(hi, g);
                        drawSegment(rawA, rawB, col);
                    }
                };
                drawFamily(true);
                drawFamily(false);
                renderer->popEnableSorting();
            }
        }
    }
    void SketcherObjWidget::drawSketchAxes()
    {
        // The axes are the one kind of curve that is drawn from its line and not from its
        // sampled points: the geometry has to start at the origin (that is the root
        // point), so it could never cover the whole axis the user sees and clicks.
        //
        // They are drawn before the sketch's own geometry because they are references,
        // and because a curve drawn exactly on one of them overlaps it pixel for pixel:
        // whichever is drawn last is the one that is seen, and it has to be the curve
        // the user drew here.
        const Eigen::Vector4<uint8_t>& preselectColor = m_drawOption.preselectColor;
        const Eigen::Vector4<uint8_t>& selectColor = m_drawOption.selectColor;
        const float pointSize = m_drawOption.pointSize;
        // Drawn before the geometry, so the width the geometry pass pushes is not in
        // effect yet: the axes carry their own.
        renderer->pushSize(m_drawOption.axisLineWidth);
        // The axes go into the depth-tested batch with the grid: they are part of the
        // scene like it, so a solid in front of the sketch plane hides them. The root
        // point is nudged off the plane together with them, so that it stays on the
        // axis it starts. (See drawBackground for why the overlay batch is wrong.)
        const BackdropDepth depth = MakeBackdropDepth(m_sceneView ? m_sceneView->GetCamera() : nullptr);
        renderer->pushEnableSorting(true);
        const int axisIds[2] = { Sketcher::GeoEnum::VAxis, Sketcher::GeoEnum::HAxis };
        for (const int geoId : axisIds) {
            // The line and the origin are two elements, and the highlight follows the one
            // that was picked: naming the point must not light the whole axis up, or every
            // pick of the origin - the point a sketch is most often constrained to - would
            // paint an infinite line as selected. It is the rule the sketch's own geometry
            // follows as well (only a whole-curve pick highlights a curve).
            const bool isLinePicked = [this, geoId]() {
                for (const SelectGeoId& sel : m_sketch->getSelectGeoPosIds()) {
                    if (sel.GeoId == geoId && sel.pointPos == PointPos::none) {
                        return true;
                    }
                }
                return false;
                }();
            const bool isOriginPicked = [this, geoId]() {
                for (const SelectGeoId& sel : m_sketch->getSelectGeoPosIds()) {
                    if (sel.GeoId != geoId) {
                        continue;
                    }
                    // A whole-curve pick covers the point it starts at, the way it does
                    // for every other curve.
                    if (sel.pointPos == PointPos::none
                        || sel.pointPos == PointPos::start) {
                        return true;
                    }
                }
                return false;
                }();
            const bool isLinePreSelected = m_sketch->getPreSelectGeoId().GeoId == geoId
                && selectState != OperationGeo
                && m_sketch->getPreSelectGeoId().pointPos == PointPos::none;
            const bool isOriginPreSelected = m_sketch->getPreSelectGeoId().GeoId == geoId
                && selectState != OperationGeo
                && (m_sketch->getPreSelectGeoId().pointPos == PointPos::none
                    || m_sketch->getPreSelectGeoId().pointPos == PointPos::start);

            // The horizontal axis is the x axis and the vertical one the y axis, so each
            // is drawn in the colour its name stands for.
            const bool isVertical = geoId == Sketcher::GeoEnum::VAxis;
            const Eigen::Vector4<uint8_t>& axisColor = isVertical
                ? m_drawOption.yAxisColor
                : m_drawOption.xAxisColor;
            renderer->pushColor(isLinePicked ? selectColor
                                             : (isLinePreSelected ? preselectColor
                                                                  : axisColor));
            drawAxisSpanning(isVertical ? 1 : 0);
            renderer->popColor();

            // The root point: the start of the horizontal axis, i.e. the sketch origin.
            // It is the one point of an axis that is a feature of its own - the far end of
            // the defining segment is not - and it is drawn with the horizontal axis only,
            // because that is the element it belongs to (GeoEnum gives RtPnt and HAxis the
            // same id, -1). Drawn by both axes, the idle dot of the one drawn last would
            // cover the highlight of the other.
            if (!isVertical) {
                renderer->drawPoint(
                    depth.place(m_sketch->plane().valueEigen(0.0, 0.0)),
                    pointSize + 1,
                    isOriginPicked ? selectColor
                                   : (isOriginPreSelected ? preselectColor
                                                          : m_drawOption.externalColor));
            }
        }
        renderer->popEnableSorting();
        renderer->popSize();
    }
    void SketcherObjWidget::drawAxisSpanning(int p_axisIndex)
    {
        // An axis is drawn as the infinite line it stands for: its geometry is a
        // segment that *starts* at the origin - the start point of the horizontal
        // axis is the root point, so it has to stay there - while the line the user
        // sees and clicks is the whole axis. Each axis line is intersected with the
        // viewport rectangle and the part between the two crossings is drawn, so the
        // span follows the view instead of being a huge piece of geometry.
        const auto* axisCam = m_sceneView ? m_sceneView->GetCamera() : nullptr;
        if (axisCam == nullptr) {
            return;
        }
        const auto& proj = axisCam->GetProjectionMatrix();
        const float proj11 = proj(1, 1);
        const BackdropDepth depth = MakeBackdropDepth(axisCam);
        // The span the caller computed is the part on screen; an axis runs along the
        // view direction as well, so the part past a clip plane is painted on that plane
        // rather than lost (see BackdropDepth).
        const auto drawSpan = [this, &depth](const Eigen::Vector3f& p_a, const Eigen::Vector3f& p_b) {
            const BackdropSegment segment = splitBackdropSegment(depth, p_a, p_b);
            for (int i = 0; i < segment.count; ++i) {
                renderer->drawLine(segment.pieces[i].a, segment.pieces[i].b);
            }
        };
        const auto& fd = m_sceneView->GetRenderer().GetFrameDescriptor();
        const float screenH = static_cast<float>(fd.renderHeight);
        const float aspect = screenH > 0.0f
            ? static_cast<float>(fd.renderWidth) / screenH
            : 1.0f;

        if (proj11 <= 0.0f
            || axisCam->GetProjectionMode() != ::Rendering::Settings::EProjectionMode::ORTHOGRAPHIC) {
            // No view rectangle to intersect with (a sketch is edited with an
            // orthographic camera, so this is only a fallback): a fixed span.
            const double extent = 500.0;
            if (p_axisIndex == 0) {
                drawSpan(
                    m_sketch->plane().valueEigen(-extent, 0.0),
                    m_sketch->plane().valueEigen(extent, 0.0));
            }
            else {
                drawSpan(
                    m_sketch->plane().valueEigen(0.0, -extent),
                    m_sketch->plane().valueEigen(0.0, extent));
            }
            return;
        }

        const auto& viewM = axisCam->GetViewMatrix();
        const Maths::FVector3 origin3(m_sketch->plane().origin.x, m_sketch->plane().origin.y, m_sketch->plane().origin.z);
        const Maths::FVector3 v0 = viewM.MulPoint(origin3);
        const float halfH = 1.0f / proj11;
        const float hx = halfH * aspect;
        const float hy = halfH;
        const Base::Vector3d dir = p_axisIndex == 0 ? m_sketch->plane().xAxis : m_sketch->plane().yAxis;
        // The direction of the axis in view space: the axis is a line through the
        // origin, so this is what tells where it leaves the viewport rectangle.
        const Maths::FVector3 p1 = viewM.MulPoint(Maths::FVector3(
            m_sketch->plane().origin.x + dir.x,
            m_sketch->plane().origin.y + dir.y,
            m_sketch->plane().origin.z + dir.z));
        const float coords[2] = { v0.x, v0.y };
        const float dirC[2] = { p1.x - v0.x, p1.y - v0.y };
        const float half[2] = { hx, hy };
        float lo = -1e9f;
        float hi = 1e9f;
        for (int c = 0; c < 2; ++c) {
            if (std::abs(dirC[c]) < 1e-9f) {
                if (std::abs(coords[c]) > half[c]) {
                    return;  // the axis lies outside the view
                }
            }
            else {
                const float t1 = (-half[c] - coords[c]) / dirC[c];
                const float t2 = (half[c] - coords[c]) / dirC[c];
                lo = std::max(lo, std::min(t1, t2));
                hi = std::min(hi, std::max(t1, t2));
            }
        }
        if (hi < lo) {
            return;
        }
        // A little beyond the crossings so the line reaches the border.
        const float ext = std::max((hi - lo) * 0.05f, 1e-3f);
        if (p_axisIndex == 0) {
            drawSpan(
                m_sketch->plane().valueEigen(lo - ext, 0.0),
                m_sketch->plane().valueEigen(hi + ext, 0.0));
        }
        else {
            drawSpan(
                m_sketch->plane().valueEigen(0.0, lo - ext),
                m_sketch->plane().valueEigen(0.0, hi + ext));
        }
    }


    void SketcherObjWidget::draw() {
        if (InEdit()) {
            drawBackground();
            // The backdrop - the grid and the axes - goes into the depth-tested batch,
            // the sketch's own geometry below into the overlay one, so the scene occludes
            // the backdrop but never the curves being edited. Within the backdrop the
            // axes come last: a line drawn exactly on one of them overlaps it pixel for
            // pixel, so whichever is drawn last hides the other (see drawSketchAxes).
            drawSketchAxes();
        }
        renderer->pushSize(m_drawOption.curveLineWidth);
       
        if (selectState == DragRect && sketchDrawRect && !isHaveActiveHandler) {
            Eigen::Vector3f p1 = m_sketch->plane().valueEigen(Base::Vector2d(std::min(onSketchPosP1.x, onSketchPosP2.x), std::min(onSketchPosP1.y, onSketchPosP2.y)));
            Eigen::Vector3f p2 = m_sketch->plane().valueEigen(Base::Vector2d(std::max(onSketchPosP1.x, onSketchPosP2.x), std::min(onSketchPosP1.y, onSketchPosP2.y)));
            Eigen::Vector3f p3 = m_sketch->plane().valueEigen(Base::Vector2d(std::max(onSketchPosP1.x, onSketchPosP2.x), std::max(onSketchPosP1.y, onSketchPosP2.y)));
            Eigen::Vector3f p4 = m_sketch->plane().valueEigen(Base::Vector2d(std::min(onSketchPosP1.x, onSketchPosP2.x), std::max(onSketchPosP1.y, onSketchPosP2.y)));
            renderer->drawQuad(p1, p2, p3, p4);
        }
        const Eigen::Vector4<uint8_t>& pointColor = m_drawOption.pointColor;
        const Eigen::Vector4<uint8_t>& preselectColor = m_drawOption.preselectColor;
        const Eigen::Vector4<uint8_t>& selectColor = m_drawOption.selectColor;
        const float pointSize = m_drawOption.pointSize;
        // Geometry projected in from another feature: drawn *before* the sketch's own
        // curves, because the sketch's own are what the user is drawing. Tracing a
        // contour runs exactly along the reference, and whichever of the two is drawn
        // last hides the other pixel for pixel (the same reason the axes are a
        // backdrop, see drawSketchAxes), so the reference has to go underneath.
        // It is drawn in its own colour and always solid, because it is a reference -
        // the sketch may constrain to it but never edits it, and it must not be
        // mistaken for something drawn here. External geometry is selectable like the
        // sketch's own curves (it is what a constraint is applied to), so it is
        // highlighted like them - only its idle colour sets it apart as a reference.
        for (int externalIndex = 0; externalIndex < m_sketch->getExternalCurveCount(); ++externalIndex) {
            const int geoId = m_sketch->getExternalGeoId(externalIndex);
            const Part::Geometry* externalGeo = m_sketch->getExternalCurve(geoId);
            if (externalGeo == nullptr) {
                continue;
            }
            const bool isSelected = [this, geoId]() {
                for (const SelectGeoId& sel : m_sketch->getSelectGeoPosIds()) {
                    if (sel.GeoId == geoId) {
                        return true;
                    }
                }
                return false;
                }();
            const bool isPreSelected = m_sketch->getPreSelectGeoId().GeoId == geoId
                && selectState != OperationGeo;

            if (m_sketch->isAxisCurve(geoId)) {
                continue;  // drawn as a backdrop, before the geometry (drawSketchAxes)
            }

            auto& segment = m_sketch->segmentOf(const_cast<Part::Geometry*>(externalGeo));

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
                        m_sketch->plane().valueEigen(segment.point[k].x, segment.point[k].y),
                        m_sketch->plane().valueEigen(segment.point[k + 1].x, segment.point[k + 1].y)
                    );
                }
            }
            renderer->popColor();

            // The markers are what the user snaps to and constrains against, so they
            // are drawn as points - endpoints and centres, like the sketch's own.
            for (int k = 0; k < static_cast<int>(segment.sepoints.size()); ++k) {
                const PointPos pos = segment.sepoints[k].pointPos;
                bool pointSelected = false;
                for (const SelectGeoId& sel : m_sketch->getSelectGeoPosIds()) {
                    if (sel.GeoId == geoId
                        && (sel.pointPos == PointPos::none || sel.pointPos == pos)) {
                        pointSelected = true;
                        break;
                    }
                }
                const bool pointPreSelected
                    = isPreSelected && m_sketch->getPreSelectGeoId().pointPos == pos;
                renderer->drawPoint(
                    m_sketch->plane().valueEigen(segment.sepoints[k].coord.x, segment.sepoints[k].coord.y),
                    pointSize + 1,
                    pointSelected ? selectColor
                                  : (pointPreSelected ? preselectColor
                                                      : m_drawOption.externalColor));
            }
        }
        for (int i = 0;i < m_sketch->geometries().size();i++) {
            if (!m_sketch->isGeometryVisible(i)) {
                continue;
            }
            bool isSelect = false;
            for (int j = 0;j < m_sketch->getSelectGeoPosIds().size();j++) {
                if (m_sketch->getSelectGeoPosIds()[j].GeoId == i) {
                    if (m_sketch->getSelectGeoPosIds()[j].pointPos == PointPos::none) {
                        isSelect = true;
                    }
                }
            }
            auto& geo = m_sketch->geometries()[i];
            const bool isConstruction
                = m_sketch->isConstructionGeometry(i) != 0 || geo->getConstruction();
            if (isSelect) {
                renderer->pushColor(selectColor);
            }
            else if (i == m_sketch->getPreSelectGeoId().GeoId && selectState != OperationGeo) {
                renderer->pushColor(preselectColor);
            }
            else {
                renderer->pushColor(
                    isConstruction ? m_drawOption.constructionColor : m_drawOption.curveColor
                );
            }
            if (geo->isDerivedFrom<Part::GeomCurve>()) {
                auto& seg = m_sketch->segmentOf(geo.get());
                if (isConstruction) {
                    drawDashedSketchPolyline(renderer, m_sketch->plane(), seg.point);
                }
                else {
                    for (int k = 0; k + 1 < static_cast<int>(seg.point.size()); k++) {
                        renderer->drawLine(
                            m_sketch->plane().valueEigen(seg.point[k].x, seg.point[k].y),
                            m_sketch->plane().valueEigen(seg.point[k + 1].x, seg.point[k + 1].y)
                        );
                    }
                }
            }
            renderer->popColor();
        }
        // Point markers are drawn after the curves so they stay on top.
        for (int geoIndex = 0; geoIndex < static_cast<int>(m_sketch->geometries().size()); ++geoIndex) {
            if (!m_sketch->isGeometryVisible(geoIndex)) {
                continue;
            }
            const bool isConstruction
                = m_sketch->isConstructionGeometry(geoIndex) != 0
                || m_sketch->geometries()[geoIndex]->getConstruction();
            auto& sePoints = m_sketch->segmentOf(m_sketch->geometries()[geoIndex].get()).sepoints;
            for (int k = 0; k < static_cast<int>(sePoints.size()); ++k) {
                bool pointSelected = false;
                for (const auto& sel : m_sketch->getSelectGeoPosIds()) {
                    if (sel.GeoId == geoIndex && sel.pointPos == sePoints[k].pointPos) {
                        pointSelected = true;
                        break;
                    }
                }
                if (pointSelected) {
                    continue;  // already drawn in its selection colour above
                }
                renderer->drawPoint(
                    m_sketch->plane().valueEigen(sePoints[k].coord.x, sePoints[k].coord.y),
                    pointSize + 1,
                    isConstruction ? m_drawOption.constructionColor : pointColor
                );
            }
        }
        // Selected points are drawn last so they stay clearly on top.
        for (const auto& sel : m_sketch->getSelectGeoPosIds()) {
            if (sel.GeoId < 0 || sel.GeoId >= static_cast<int>(m_sketch->geometries().size())) {
                continue;
            }
            if (!m_sketch->isGeometryVisible(sel.GeoId)) {
                continue;
            }
            auto& sePoints = m_sketch->segmentOf(m_sketch->geometries()[sel.GeoId].get()).sepoints;
            for (const auto& sp : sePoints) {
                if (sp.pointPos == sel.pointPos) {
                    renderer->drawPoint(
                        m_sketch->plane().valueEigen(sp.coord.x, sp.coord.y),
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
    void SketcherObjWidget::updateConstraintLabelInteraction()
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
            const Sketcher::Constraint* c = m_sketch->getConstraint(m_labelDrag);
            if (!c) {
                m_labelDrag = -1;
                return;
            }
            const bool straightDim = isStraightDimension(c->Type);
            if (straightDim && m_labelDragHandle != LabelHandle::Caption) {
                // The dimension line is being dragged: it moves as a whole, and
                // all it does is move along the direction its extension lines run in -
                // the cursor's projection onto that direction is the new offset. The
                // line keeps its orientation, so it can never come out tilted.
                StraightDimFrame frame;
                if (straightDimFrame(c, frame)) {
                    // Projecting from either base gives the same value: the two bases
                    // differ along the line, which the projection onto the direction
                    // ignores.
                    const float wanted
                        = (static_cast<float>(mx) - frame.baseA.x()) * frame.direction.x()
                        + (static_cast<float>(my) - frame.baseA.y()) * frame.direction.y();
                    // Keep the line on its side of the geometry: a dimension that
                    // crosses what it measures is never what the user meant.
                    //constexpr float kMinOffset = 8.0f;
                    // The offset is stored in sketch units, so it is taken from the
                    // cursor in pixels and turned into the drawing's own scale.
                    m_straightDimOffsetSketch[c] = wanted / pixelsPerSketchUnit();
                }
                m_labelHover = m_labelDrag;
                m_labelHoverHandle = m_labelDragHandle;
                return;
            }
            if (c->Type == Sketcher::ConstraintType::Angle
                && m_labelDragHandle == LabelHandle::AngleArc) {
                // The arc is being dragged: its centre is the vertex of the angle and
                // does not move, so what changes is the radius - the arc is drawn at the
                // distance the cursor is at, and therefore covers more or less length.
                float centerX = 0.0f, centerY = 0.0f;
                float radius = 0.0f, startDeg = 0.0f, sweepDeg = 0.0f;
                if (computeAngleLabelTrack(c, centerX, centerY, radius, startDeg, sweepDeg)) {
                    const float dx = static_cast<float>(mx) - centerX;
                    const float dy = static_cast<float>(my) - centerY;
                    constexpr float kMinRadius = 8.0f;
                    const float radiusPx = std::max(std::sqrt(dx * dx + dy * dy), kMinRadius);
                    m_angleLabelRadiusSketch[c] = radiusPx / pixelsPerSketchUnit();
                }
                m_labelHover = m_labelDrag;
                m_labelHoverHandle = m_labelDragHandle;
                return;
            }
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
                // A radial caption has no track to slide along: what it carries is the
                // direction its leader runs in, and that is simply the one from the
                // centre of the circle to where the cursor is on the sketch plane - the
                // same thing the preview follows, so the two cannot drift apart.
                // (Accumulating screen deltas instead only worked face on: under a
                // rotated camera a screen delta is not a sketch delta, and the label
                // would lag behind the cursor.)
                Base::Vector2d center;
                if (m_sketch->getGeometryCenterSketch(c->First, center)) {
                    const Base::Vector2d cursor = getMouseHitSketchPlanePoint();
                    const Base::Vector2d wanted = cursor - center;
                    if (wanted.Length() > 1.0e-9) {
                        m_labelManualOffsetSketch[c] = wanted;
                    }
                }
            }
            m_labelHover = m_labelDrag;
            m_labelHoverHandle = m_labelDragHandle;
            return;
        }
        pickLabelTarget(
            static_cast<float>(mx), static_cast<float>(my), m_labelHover, m_labelHoverHandle);
    }
    bool SketcherObjWidget::getConstraintMeasureEndpoints(
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
                return m_sketch->getGeometryPointSketch(constraint->First, constraint->FirstPos, a)
                    && m_sketch->getGeometryPointSketch(constraint->Second, constraint->SecondPos, b);
            }
            if (constraint->FirstPos == PointPos::none) {
                return m_sketch->getGeometryPointSketch(constraint->First, PointPos::start, a)
                    && m_sketch->getGeometryPointSketch(constraint->First, PointPos::end, b);
            }
            // Coordinate constraint of a single point: DistanceX fixes the
            // x-coordinate, so the shaft spans from the point to the Y axis;
            // DistanceY fixes the y-coordinate and spans to the X axis.
            if (!m_sketch->getGeometryPointSketch(constraint->First, constraint->FirstPos, a)) {
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
                return m_sketch->getGeometryPointSketch(constraint->First, PointPos::start, a)
                    && m_sketch->getGeometryPointSketch(constraint->First, PointPos::end, b);
            }
            if (constraint->FirstPos != PointPos::none
                && constraint->SecondPos == PointPos::none) {
                Base::Vector2d la, lb;
                if (m_sketch->getGeometryPointSketch(constraint->First, constraint->FirstPos, a)
                    && m_sketch->getGeometryPointSketch(constraint->Second, PointPos::start, la)
                    && m_sketch->getGeometryPointSketch(constraint->Second, PointPos::end, lb)) {
                    projectPointOnSegment2d(a, la, lb, b);
                    return true;
                }
                return false;
            }
            if (constraint->FirstPos != PointPos::none
                && constraint->SecondPos != PointPos::none) {
                return m_sketch->getGeometryPointSketch(constraint->First, constraint->FirstPos, a)
                    && m_sketch->getGeometryPointSketch(constraint->Second, constraint->SecondPos, b);
            }
            return m_sketch->getGeometryCenterSketch(constraint->First, a)
                && m_sketch->getGeometryCenterSketch(constraint->Second, b);
        }
        default:
            return false;
        }
    }
    bool SketcherObjWidget::computeStraightLabelTrack(
        const Sketcher::Constraint* constraint,
        float& trackAx,
        float& trackAy,
        float& trackBx,
        float& trackBy,
        float& gapX,
        float& gapY
    ) const
    {
        StraightDimFrame frame;
        if (!straightDimFrame(constraint, frame)) {
            return false;
        }
        // One offset for the whole line: the dimension is dragged as a unit and keeps
        // its direction, so it can never come out tilted.
        const float offset = straightDimOffset(constraint, frame.defaultOffset);
        trackAx = frame.baseA.x() + frame.direction.x() * offset;
        trackAy = frame.baseA.y() + frame.direction.y() * offset;
        trackBx = frame.baseB.x() + frame.direction.x() * offset;
        trackBy = frame.baseB.y() + frame.direction.y() * offset;
        gapX = frame.gapX;
        gapY = frame.gapY;
        return true;
    }
    bool SketcherObjWidget::straightDimFrame(
        const Sketcher::Constraint* constraint,
        StraightDimFrame& out
    ) const
    {
        if (constraint == nullptr) {
            return false;
        }
        Base::Vector2d aSk, bSk;
        if (!getConstraintMeasureEndpoints(constraint, aSk, bSk)) {
            return false;
        }
        const auto worldOf = [this](const Base::Vector2d& sk) {
            return m_sketch->plane().origin + sk.x * m_sketch->plane().xAxis + sk.y * m_sketch->plane().yAxis;
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
        const float captionGap = 14.0f;

        // The automatic offset is a share of what the dimension measures, in sketch
        // units: a note that keeps its distance from the geometry on the drawing
        // rather than on the screen, so zooming does not move it.
        const double span = (bSk - aSk).Length();
        const float autoOffsetSketch
            = static_cast<float>(std::max(span * 0.1, 1.0e-4));

        out.measuredA = aSk;
        out.measuredB = bSk;
        out.screenA = aS;
        out.screenB = bS;
        if (isHorizDist || isVertDist) {
            // Horizontal/vertical distances always draw an axis-aligned
            // dimension shaft instead of a line parallel to the measured
            // segment: DistanceX stays horizontal above the points, DistanceY
            // stays vertical to the right of them.
            if (isHorizDist) {
                // The line runs above both points and stays horizontal until an end is
                // dragged; each end only moves straight up (its extension line).
                const float lineY = std::min(aS.y(), bS.y());
                out.baseA = Eigen::Vector2f(aS.x(), lineY);
                out.baseB = Eigen::Vector2f(bS.x(), lineY);
                out.direction = Eigen::Vector2f(0.0f, -1.0f);
                out.defaultOffset = autoOffsetSketch;
                out.gapX = 0.0f;
                out.gapY = -captionGap;
            }
            else {
                // Vertical distance: shaft to the right of both points (vertical until
                // dragged), caption between the measured points and the shaft.
                const float lineX = std::max(aS.x(), bS.x());
                out.baseA = Eigen::Vector2f(lineX, aS.y());
                out.baseB = Eigen::Vector2f(lineX, bS.y());
                out.direction = Eigen::Vector2f(1.0f, 0.0f);
                // The caption sits between the geometry and the line, so this one is a
                // little further out.
                out.defaultOffset = autoOffsetSketch * 1.6f;
                out.gapX = -captionGap;
                out.gapY = 0.0f;
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
        // The default caption place only says which side of the geometry the dimension
        // lives on; how far out it sits is the share of the span from above.
        const float shaftDist = autoOffsetSketch;
        // A plain length dimension sits parallel to the segment it measures; both ends
        // start on the points themselves and may be pulled along that normal.
        out.baseA = aS;
        out.baseB = bS;
        out.direction = Eigen::Vector2f(nx, ny);
        out.defaultOffset = shaftDist;
        out.gapX = nx * captionGap;
        out.gapY = ny * captionGap;
        return true;
    }
    float SketcherObjWidget::pixelsPerSketchUnit() const
    {
        // One sketch unit is measured on screen through the same mapping the
        // annotations are drawn with, so that the two cannot disagree about the
        // scale. The two axes give the same answer for a sketch seen from the front,
        // which is what the dimension overlay is for.
        const auto worldOf = [this](const Base::Vector2d& sk) {
            return m_sketch->plane().origin + sk.x * m_sketch->plane().xAxis + sk.y * m_sketch->plane().yAxis;
            };
        const auto screenOf = [this, &worldOf](const Base::Vector2d& sk) {
            const Base::Vector3d w = worldOf(sk);
            return renderer->worldToScreen(Eigen::Vector3f(
                static_cast<float>(w.x), static_cast<float>(w.y), static_cast<float>(w.z)));
            };
        const Eigen::Vector2f origin = screenOf(Base::Vector2d(0.0, 0.0));
        const Eigen::Vector2f unitX = screenOf(Base::Vector2d(1.0, 0.0));
        const Eigen::Vector2f unitY = screenOf(Base::Vector2d(0.0, 1.0));
        const float scale = 0.5f * ((unitX - origin).norm() + (unitY - origin).norm());
        // A sketch seen edge on collapses to nothing; the annotations keep a workable
        // size instead of collapsing with it.
        return scale > 1.0e-4f ? scale : 1.0e-4f;
    }

    void SketcherObjWidget::placeDimensionAnnotation(int constrId, float p_screenX, float p_screenY)
    {
        const Sketcher::Constraint* c = m_sketch->getConstraint(constrId);
        if (c == nullptr) {
            return;
        }
        // A straight dimension: the shaft sits as far from the measured geometry as
        // the given place does, which is exactly what dragging the shaft there would
        // have stored. The caption keeps the middle of the shaft.
        if (c->Type == Sketcher::ConstraintType::Distance
            || c->Type == Sketcher::ConstraintType::DistanceX
            || c->Type == Sketcher::ConstraintType::DistanceY) {
            StraightDimFrame frame;
            if (straightDimFrame(c, frame)) {
                // The drop point arrives in screen pixels, the layout is kept in
                // sketch units: the same conversion the dragging code uses, so the
                // annotation ends up exactly where the preview had it.
                const float offsetPx
                    = (p_screenX - frame.baseA.x()) * frame.direction.x()
                    + (p_screenY - frame.baseA.y()) * frame.direction.y();
                m_straightDimOffsetSketch[c] = offsetPx / pixelsPerSketchUnit();
            }
            m_labelManualParam[c] = 0.5;
            return;
        }
        // An angle: the annotation arc is drawn at the distance of the given place
        // from the vertex, which is what pulling the arc in and out does.
        if (c->Type == Sketcher::ConstraintType::Angle) {
            float centerX = 0.0f, centerY = 0.0f;
            float radius = 0.0f, startDeg = 0.0f, sweepDeg = 0.0f;
            if (computeAngleLabelTrack(c, centerX, centerY, radius, startDeg, sweepDeg)) {
                const float dx = p_screenX - centerX;
                const float dy = p_screenY - centerY;
                const float radiusPx = std::max(std::sqrt(dx * dx + dy * dy), 8.0f);
                m_angleLabelRadiusSketch[c] = radiusPx / pixelsPerSketchUnit();
            }
            m_labelManualParam[c] = 0.5;
            return;
        }
        // A radius or a diameter: only the direction from the centre matters, so the
        // radial line and its caption end up on the side the tool pointed at.
        if (c->Type == Sketcher::ConstraintType::Radius
            || c->Type == Sketcher::ConstraintType::Diameter) {
            Base::Vector2d centerSk;
            if (!m_sketch->getGeometryCenterSketch(c->First, centerSk)) {
                return;
            }
            const Base::Vector3d world
                = m_sketch->plane().origin + centerSk.x * m_sketch->plane().xAxis + centerSk.y * m_sketch->plane().yAxis;
            const Eigen::Vector2f centerS = renderer->worldToScreen(
                Eigen::Vector3f(
                    static_cast<float>(world.x),
                    static_cast<float>(world.y),
                    static_cast<float>(world.z)
                )
            );
            // Only the direction is kept, and in sketch space: the screen offset the
            // tool dropped has to be turned into the direction it means there, or a
            // rotated camera would put the leader somewhere else than the drop point.
            Base::Vector2d direction;
            if (sketchVectorOfScreenVector(
                Eigen::Vector2f(p_screenX - centerS.x(), p_screenY - centerS.y()),
                direction)) {
                m_labelManualOffsetSketch[c] = direction;
            }
        }
    }
    float SketcherObjWidget::straightDimOffset(
        const Sketcher::Constraint* constraint,
        float p_defaultOffsetSketch
    ) const
    {
        // Kept in sketch units so that the dimension stays where it was put while the
        // view is zoomed; the drawing needs pixels.
        const auto found = m_straightDimOffsetSketch.find(constraint);
        const float offsetSketch
            = found == m_straightDimOffsetSketch.end() ? p_defaultOffsetSketch : found->second;
        return offsetSketch * pixelsPerSketchUnit();
    }
    bool SketcherObjWidget::straightDimShaft(
        const Sketcher::Constraint* constraint,
        Eigen::Vector2f& p_a,
        Eigen::Vector2f& p_b
    ) const
    {
        float trackAx = 0.0f, trackAy = 0.0f, trackBx = 0.0f, trackBy = 0.0f;
        float gapX = 0.0f, gapY = 0.0f;
        if (!computeStraightLabelTrack(constraint, trackAx, trackAy, trackBx, trackBy, gapX, gapY)) {
            return false;
        }
        p_a = Eigen::Vector2f(trackAx, trackAy);
        p_b = Eigen::Vector2f(trackBx, trackBy);
        return true;
    }
    int SketcherObjWidget::pickConstraintDimLineAt(float p_mouseX, float p_mouseY) const
    {
        if (!InEdit()) {
            return -1;
        }
        constexpr float kLineTolerance = 8.0f;
        const Eigen::Vector2f mouse(p_mouseX, p_mouseY);
        int best = -1;
        float bestDistance = kLineTolerance;
        // Last constraint first, like the caption hit test: what was added later sits
        // on top.
        for (int i = static_cast<int>(m_sketch->constraints().size()) - 1; i >= 0; --i) {
            const Sketcher::Constraint* c = m_sketch->constraints()[i];
            if (!c || !c->isVisible || !isStraightDimension(c->Type)) {
                continue;
            }
            Eigen::Vector2f a;
            Eigen::Vector2f b;
            if (!straightDimShaft(c, a, b)) {
                continue;
            }
            // Distance to the segment, so the arrows and everything between them are
            // the same handle.
            const Eigen::Vector2f direction = b - a;
            const float length = direction.norm();
            float distance = (mouse - a).norm();
            if (length > 1.0f) {
                const float t = std::clamp(
                    (mouse - a).dot(direction) / (length * length), 0.0f, 1.0f);
                distance = (mouse - (a + direction * t)).norm();
            }
            if (distance < bestDistance) {
                bestDistance = distance;
                best = i;
            }
        }
        return best;
    }
    int SketcherObjWidget::pickConstraintAngleArcAt(float p_mouseX, float p_mouseY) const
    {
        if (!InEdit()) {
            return -1;
        }
        constexpr float kArcTolerance = 8.0f;
        constexpr float kPi = 3.14159265358979f;
        for (int i = static_cast<int>(m_sketch->constraints().size()) - 1; i >= 0; --i) {
            const Sketcher::Constraint* c = m_sketch->constraints()[i];
            if (!c || !c->isVisible || c->Type != Sketcher::ConstraintType::Angle) {
                continue;
            }
            float centerX = 0.0f, centerY = 0.0f;
            float radius = 0.0f, startDeg = 0.0f, sweepDeg = 0.0f;
            if (!computeAngleLabelTrack(c, centerX, centerY, radius, startDeg, sweepDeg)) {
                continue;
            }
            const float dx = p_mouseX - centerX;
            const float dy = p_mouseY - centerY;
            const float distance = std::sqrt(dx * dx + dy * dy);
            if (std::fabs(distance - radius) > kArcTolerance) {
                continue;  // not on the arc itself
            }
            // ... and within the swept part of it, so the empty side of the circle does
            // not grab the cursor.
            const float angleDeg = std::atan2(dy, dx) * 180.0f / kPi;
            float relative = angleDeg - startDeg;
            if (sweepDeg >= 0.0f) {
                while (relative < 0.0f) {
                    relative += 360.0f;
                }
                while (relative > 360.0f) {
                    relative -= 360.0f;
                }
                if (relative > sweepDeg + 1.0f) {
                    continue;
                }
            }
            else {
                while (relative > 0.0f) {
                    relative -= 360.0f;
                }
                while (relative < -360.0f) {
                    relative += 360.0f;
                }
                if (relative < sweepDeg - 1.0f) {
                    continue;
                }
            }
            return i;
        }
        return -1;
    }
    void SketcherObjWidget::pickLabelTarget(
        float p_mouseX,
        float p_mouseY,
        int& p_constrId,
        LabelHandle& p_handle
    ) const
    {
        p_constrId = -1;
        p_handle = LabelHandle::Caption;
        // The caption is a small box, the line a long thin target, so the caption is
        // tested first: pointing at the text means "move the text", not the line.
        p_constrId = pickConstraintLabelAt(p_mouseX, p_mouseY);
        if (p_constrId >= 0) {
            return;
        }
        p_constrId = pickConstraintDimLineAt(p_mouseX, p_mouseY);
        if (p_constrId >= 0) {
            p_handle = LabelHandle::DimensionLine;
            return;
        }
        p_constrId = pickConstraintAngleArcAt(p_mouseX, p_mouseY);
        if (p_constrId >= 0) {
            p_handle = LabelHandle::AngleArc;
        }
    }
    
    bool SketcherObjWidget::computeAngleLabelTrack(
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
            if (m_sketch->getGeometryPointSketch(constraint->First, PointPos::start, p1)
                && m_sketch->getGeometryPointSketch(constraint->First, PointPos::end, p2)) {
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
            if (m_sketch->getGeometryPointSketch(constraint->First, PointPos::start, a1)
                && m_sketch->getGeometryPointSketch(constraint->First, PointPos::end, a2)
                && m_sketch->getGeometryPointSketch(constraint->Second, PointPos::start, b1)
                && m_sketch->getGeometryPointSketch(constraint->Second, PointPos::end, b2)
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
            return m_sketch->plane().origin + sk.x * m_sketch->plane().xAxis + sk.y * m_sketch->plane().yAxis;
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
        // The arc is part of the drawing, so its default size is a share of the lines
        // it measures - in sketch units. It therefore keeps its place when the view is
        // zoomed, exactly like the length dimensions do. (A single line against the x
        // axis has only the segment itself to measure against: the other ray is a unit
        // vector standing for the axis.)
        const double referenceLength = constraint->Second == Sketcher::GeoEnum::GeoUndef
            ? dir2.Length()
            : std::min(dir1.Length(), dir2.Length());
        const float defaultRadiusSketch = static_cast<float>(
            std::max(referenceLength * 0.3, 1.0e-4));
        radiusPx = defaultRadiusSketch * pixelsPerSketchUnit();
        // The arc can be pulled closer or pushed further out; its centre stays on the
        // vertex, so the radius is the only thing that moves - and the drawn arc grows
        // and shrinks with it (the sweep never changes).
        const auto dragged = m_angleLabelRadiusSketch.find(constraint);
        if (dragged != m_angleLabelRadiusSketch.end()) {
            radiusPx = dragged->second * pixelsPerSketchUnit();
        }
        centerX = vS.x();
        centerY = vS.y();
        startDeg = rawStart;
        sweepDeg = sweep;
        return true;
    }
    Base::Vector2d SketcherObjWidget::constraintLabelAnchor(const Sketcher::Constraint* c) const
    {
        Base::Vector2d p1, p2;
        if (!c) {
            return Base::Vector2d();
        }
        switch (c->Type) {
        case Sketcher::ConstraintType::DistanceX:
        case Sketcher::ConstraintType::DistanceY: {
            if (c->Second != Sketcher::GeoEnum::GeoUndef) {
                if (m_sketch->getGeometryPointSketch(c->First, c->FirstPos, p1)
                    && m_sketch->getGeometryPointSketch(c->Second, c->SecondPos, p2)) {
                    return (p1 + p2) * 0.5;
                }
            }
            else if (c->FirstPos != PointPos::none) {
                // coordinate of a single point
                if (m_sketch->getGeometryPointSketch(c->First, c->FirstPos, p1)) {
                    return p1;
                }
            }
            else if (m_sketch->getGeometryPointSketch(c->First, PointPos::start, p1)
                && m_sketch->getGeometryPointSketch(c->First, PointPos::end, p2)) {
                return (p1 + p2) * 0.5;
            }
            break;
        }
        case Sketcher::ConstraintType::Distance: {
            if (c->Second == Sketcher::GeoEnum::GeoUndef) {
                // length of a single edge: anchor on the middle of its curve
                const Part::Geometry* geo = m_sketch->resolveGeometry(c->First);
                if (geo) {
                    const SketcherObj::CurveSegment* segment = m_sketch->findSegment(geo);
                    if (segment != nullptr && !segment->point.empty()) {
                        const Base::Vector3d mid = segment->point[segment->point.size() / 2];
                        return Base::Vector2d(mid.x, mid.y);
                    }
                }
            }
            else if (c->FirstPos != PointPos::none && c->SecondPos == PointPos::none) {
                // point -> line distance
                Base::Vector2d la, lb;
                if (m_sketch->getGeometryPointSketch(c->First, c->FirstPos, p1)
                    && m_sketch->getGeometryPointSketch(c->Second, PointPos::start, la)
                    && m_sketch->getGeometryPointSketch(c->Second, PointPos::end, lb)) {
                    Base::Vector2d proj;
                    projectPointOnSegment2d(p1, la, lb, proj);
                    return (p1 + proj) * 0.5;
                }
            }
            else if (c->FirstPos == PointPos::none && c->SecondPos == PointPos::none) {
                // curve -> curve distance
                if (m_sketch->getGeometryCenterSketch(c->First, p1) && m_sketch->getGeometryCenterSketch(c->Second, p2)) {
                    return (p1 + p2) * 0.5;
                }
            }
            else if (c->FirstPos != PointPos::none && c->SecondPos != PointPos::none) {
                // point -> point distance
                if (m_sketch->getGeometryPointSketch(c->First, c->FirstPos, p1)
                    && m_sketch->getGeometryPointSketch(c->Second, c->SecondPos, p2)) {
                    return (p1 + p2) * 0.5;
                }
            }
            break;
        }
        case Sketcher::ConstraintType::Radius:
        case Sketcher::ConstraintType::Diameter: {
            if (m_sketch->getGeometryCenterSketch(c->First, p1)) {
                return p1;
            }
            break;
        }
        case Sketcher::ConstraintType::Angle: {
            if (c->Second == Sketcher::GeoEnum::GeoUndef) {
                // single line against the sketch x-axis: anchor at the start
                // so the caption stays close to the angle arc
                if (m_sketch->getGeometryPointSketch(c->First, PointPos::start, p1)) {
                    return p1;
                }
            }
            else if (c->FirstPos != PointPos::none && c->SecondPos != PointPos::none) {
                if (m_sketch->getGeometryPointSketch(c->First, c->FirstPos, p1)
                    && m_sketch->getGeometryPointSketch(c->Second, c->SecondPos, p2)) {
                    return (p1 + p2) * 0.5;
                }
            }
            else if (c->FirstPos == PointPos::none && c->SecondPos == PointPos::none) {
                // angle between two lines: use their intersection when it is
                // near the segments, otherwise fall back to the middle point
                Base::Vector2d a1, a2, b1, b2;
                if (m_sketch->getGeometryPointSketch(c->First, PointPos::start, a1)
                    && m_sketch->getGeometryPointSketch(c->First, PointPos::end, a2)
                    && m_sketch->getGeometryPointSketch(c->Second, PointPos::start, b1)
                    && m_sketch->getGeometryPointSketch(c->Second, PointPos::end, b2)) {
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
        if (m_sketch->getGeometryCenterSketch(c->First, p1)) {
            return p1;
        }
        return Base::Vector2d();
    }
    std::string SketcherObjWidget::constraintLabelText(const Sketcher::Constraint* c) const
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
    bool SketcherObjWidget::constraintInError(int constrId) const
    {
        // The sketch keeps the solver's diagnosis of the last solve; the most severe
        // entry a constraint shows up in is what a label should look like.
        return m_sketch->getConstraintStatus(constrId) != SketcherObj::ConstraintStatus::Ok;
    }
    bool SketcherObjWidget::computeConstraintLabel(
        int constrId,
        Base::Vector2d& anchorSketch,
        float& screenX,
        float& screenY
    ) const
    {
        if (constrId < 0 || constrId >= static_cast<int>(m_sketch->constraints().size())) {
            return false;
        }
        const Sketcher::Constraint* c = m_sketch->constraints()[constrId];
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
            // The caption sits just outside the rim, on the line of the shaft itself -
            // whatever direction that shaft ended up taking (see radiusDimShaft).
            Eigen::Vector2f centerS;
            Eigen::Vector2f rimS;
            if (radiusDimShaft(c, centerS, rimS)) {
                const float dx = rimS.x() - centerS.x();
                const float dy = rimS.y() - centerS.y();
                const float rimDist = std::sqrt(dx * dx + dy * dy);
                if (rimDist > 1.0f) {
                    const float labelDist = 18.0f;
                    anchorSketch = constraintLabelAnchor(c);
                    screenX = rimS.x() + dx / rimDist * labelDist;
                    screenY = rimS.y() + dy / rimDist * labelDist;
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
                // Keep the caption clear of the arc: the same gap a length dimension
                // keeps from its line, so the arc stays free to be grabbed and pulled.
                const float captionRadius = arcRadius + 14.0f;
                anchorSketch = constraintLabelAnchor(c);
                screenX = cx + captionRadius * std::cos(angleRad);
                screenY = cy + captionRadius * std::sin(angleRad);
                return true;
            }
        }
        anchorSketch = constraintLabelAnchor(c);
        const Base::Vector3d world3 = m_sketch->plane().origin + anchorSketch.x * m_sketch->plane().xAxis
            + anchorSketch.y * m_sketch->plane().yAxis;
        const Eigen::Vector3f world(world3.x, world3.y, world3.z);
        const Eigen::Vector2f screen = renderer->worldToScreen(world);
        const float perUnit = pixelsPerSketchUnit();
        // Both the hand-placed offset and the default one are sketch units, so the
        // caption travels with the drawing; only the drawing needs pixels.
        float dx = 0.0f, dy = 0.0f;
        const auto it = m_labelManualOffsetSketch.find(c);
        if (it != m_labelManualOffsetSketch.end()) {
            dx = static_cast<float>(it->second.x);
            dy = static_cast<float>(it->second.y);
        }
        else {
            defaultLabelOffsetPx(c, dx, dy);
            dx /= perUnit;
            dy /= perUnit;
        }
        screenX = screen.x() + dx * perUnit;
        screenY = screen.y() + dy * perUnit;
        return true;
    }
    int SketcherObjWidget::pickConstraintLabelAt(float mouseX, float mouseY) const
    {
        if (!InEdit()) {
            return -1;
        }
        for (int i = static_cast<int>(m_sketch->constraints().size()) - 1; i >= 0; --i) {
            const Sketcher::Constraint* c = m_sketch->constraints()[i];
            if (!c || !c->isVisible || !isDimensionLabelType(c->Type)) {
                continue;
            }
            Base::Vector2d anchor;
            float sx = 0.0f, sy = 0.0f;
            if (!computeConstraintLabel(i, anchor, sx, sy)) {
                continue;
            }
            const Im2D::Vec2 ts = Im2D::DrawList::CalcTextSize(AnnotationFont(), kAnnotationFontSize, constraintLabelText(c).c_str());
            const float padX = 8.0f, padY = 5.0f;
            if (mouseX >= sx - ts.x * 0.5f - padX && mouseX <= sx + ts.x * 0.5f + padX
                && mouseY >= sy - ts.y * 0.5f - padY && mouseY <= sy + ts.y * 0.5f + padY) {
                return i;
            }
        }
        return -1;
    }
    /** True when p_angle lies on the sweep that runs from p_start to p_end the way
     * that passes through p_through; all four in degrees. */
    static bool angleOnSweep(float p_angle, float p_start, float p_end, float p_through)
    {
        const auto normalise = [](float p_value) {
            while (p_value < 0.0f) {
                p_value += 360.0f;
            }
            while (p_value >= 360.0f) {
                p_value -= 360.0f;
            }
            return p_value;
        };
        const float angle = normalise(p_angle - p_start);
        const float end = normalise(p_end - p_start);
        const float through = normalise(p_through - p_start);
        if (through <= end) {
            return angle <= end;
        }
        // The arc runs the other way round, so it covers everything from its end
        // back to its start.
        return angle >= end;
    }

    bool SketcherObjWidget::sketchVectorOfScreenVector(
        const Eigen::Vector2f& p_screenVector,
        Base::Vector2d& p_out
    ) const
    {
        // One sketch unit along x covers uPix on screen and one along y covers vPix,
        // so the two of them are the affine map the plane is projected by; inverting it
        // turns a vector that was measured on screen into the one it means here.
        const auto screenOf = [this](const Base::Vector2d& sk) {
            return renderer->worldToScreen(m_sketch->plane().valueEigen(sk));
            };
        const Eigen::Vector2f origin = screenOf(Base::Vector2d(0.0, 0.0));
        const Eigen::Vector2f uPix = screenOf(Base::Vector2d(1.0, 0.0)) - origin;
        const Eigen::Vector2f vPix = screenOf(Base::Vector2d(0.0, 1.0)) - origin;
        const float det = uPix.x() * vPix.y() - uPix.y() * vPix.x();
        if (std::abs(det) < 1.0e-6f) {
            return false;
        }
        p_out = Base::Vector2d(
            (p_screenVector.x() * vPix.y() - uPix.y() * p_screenVector.y()) / det,
            (uPix.x() * p_screenVector.y() - p_screenVector.x() * vPix.x()) / det);
        return true;
    }

    bool SketcherObjWidget::radiusDimShaft(
        const Sketcher::Constraint* p_constraint,
        Eigen::Vector2f& p_centerScreen,
        Eigen::Vector2f& p_rimScreen
    ) const
    {
        if (p_constraint == nullptr
            || (p_constraint->Type != Sketcher::ConstraintType::Radius
                && p_constraint->Type != Sketcher::ConstraintType::Diameter)) {
            return false;
        }
        const Part::Geometry* geo = m_sketch->resolveGeometry(p_constraint->First);
        if (geo == nullptr
            || !(geo->is<Part::GeomCircle>() || geo->is<Part::GeomArcOfCircle>())) {
            return false;
        }

        // The shaft runs from the centre of the circle to a point of its rim: the
        // centre comes from the curve (see m_sketch->getGeometryCenterSketch, which has to answer
        // with the circle's centre for an arc rather than the middle of its chord), the
        // direction from where the caption was put - or the arc's own middle, which is
        // the part of the circle the arc covers - and the length is the radius itself.
        const CurveSegment* seg = m_sketch->findSegment(geo);
        Base::Vector2d centerSk;
        if (!m_sketch->getGeometryCenterSketch(p_constraint->First, centerSk)) {
            return false;
        }
        const double radius = geo->is<Part::GeomCircle>()
            ? static_cast<const Part::GeomCircle*>(geo)->getRadius()
            : static_cast<const Part::GeomArcOfCircle*>(geo)->getRadius();
        if (radius <= 0.0) {
            return false;
        }

        // The shaft is built in sketch space and projected at the end, so its two ends
        // are the centre and a point of the rim by construction. The direction is what
        // has to be chosen: an arc's own middle is the default (it is the part of the
        // circle the arc actually covers), and once the caption has been put somewhere
        // its direction is what the dimension follows - the same rule the preview
        // obeys while the annotation is being placed. FreeCAD keeps the label on the
        // arc here; following the label instead is what makes dragging it work.
        Base::Vector2d dirSk(0.70710678118, 0.70710678118);
        Base::Vector2d arcMidSk;
        if (geo->is<Part::GeomArcOfCircle>()) {
            if (seg != nullptr && seg->point.size() > 2) {
                const Base::Vector3d& mid = seg->point[seg->point.size() / 2];
                arcMidSk = Base::Vector2d(mid.x, mid.y);
                const Base::Vector2d toMid = arcMidSk - centerSk;
                if (toMid.Length() > 1.0e-9) {
                    dirSk = Base::Vector2d(
                        toMid.x / toMid.Length(),
                        toMid.y / toMid.Length());
                }
            }
        }
        const auto manualIt = m_labelManualOffsetSketch.find(p_constraint);
        if (manualIt != m_labelManualOffsetSketch.end()) {
            // Already a direction of the sketch, whichever way it was put there: the
            // tool's drop point and the caption drag both store it that way.
            const Base::Vector2d wanted = manualIt->second;
            if (wanted.Length() > 1.0e-9) {
                dirSk = Base::Vector2d(wanted.x / wanted.Length(), wanted.y / wanted.Length());
            }
        }

        p_centerScreen = renderer->worldToScreen(m_sketch->plane().valueEigen(centerSk));
        p_rimScreen = renderer->worldToScreen(m_sketch->plane().valueEigen(Base::Vector2d(
            centerSk.x + dirSk.x * radius,
            centerSk.y + dirSk.y * radius)));
        return true;
    }

    void SketcherObjWidget::drawConstraintLabels()
    {
        if (!InEdit() || m_sketch->constraints().empty()) {
            return;
        }
        // Keep the hover state in sync with the mouse even when the cursor did
        // not move (e.g. the camera was zoomed with the wheel).
        if (m_labelDrag < 0) {
            if (!isHaveActiveHandler) {
                auto [mx, my] = m_sceneView->getInutState().GetMousePosition();
                pickLabelTarget(
                    static_cast<float>(mx),
                    static_cast<float>(my),
                    m_labelHover,
                    m_labelHoverHandle
                );
            }
            else {
                m_labelHover = -1;
                m_labelHoverHandle = LabelHandle::Caption;
            }
        }
        Im2D::DrawList* drawList = &Rendering::Overlay::Im2DRender::Instance().GetDrawList();
        if (!drawList) {
            return;
        }
        const float thickness = 2.0f;
        const float pi = 3.14159265358979f;
        for (int i = 0; i < static_cast<int>(m_sketch->constraints().size()); ++i) {
            const Sketcher::Constraint* c = m_sketch->constraints()[i];
            if (!c || !c->isVisible || !isDimensionLabelType(c->Type)) {
                continue;
            }
            Base::Vector2d anchor;
            float sx = 0.0f, sy = 0.0f;
            if (!computeConstraintLabel(i, anchor, sx, sy)) {
                continue;
            }
            const std::string text = constraintLabelText(c);
            const Im2D::Vec2 ts = Im2D::DrawList::CalcTextSize(AnnotationFont(), kAnnotationFontSize, text.c_str());
            const bool isError = constraintInError(i);
            const bool hovered = (i == m_labelHover) || (i == m_labelDrag);
            const float padX = 6.0f, padY = 3.0f;
            const Im2D::Vec2 boxMin(sx - ts.x * 0.5f - padX, sy - ts.y * 0.5f - padY);
            const Im2D::Vec2 boxMax(sx + ts.x * 0.5f + padX, sy + ts.y * 0.5f + padY);
            const Im2D::ImU32 arrowCol = isError ? Im2D::ColorU32(255, 110, 110, 255)
                : abgrToImU32(m_drawOption.constraintColor);
            auto screenOf = [&](const Base::Vector2d& sk) -> Eigen::Vector2f {
                return renderer->worldToScreen(m_sketch->plane().valueEigen(sk));
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
                    // Extension lines: what the dimension line actually measures is the
                    // two points of the constraint, so each end of the line is tied back
                    // to its point. They are what makes a dragged (tilted) dimension
                    // readable, and FreeCAD draws them as well.
                    StraightDimFrame frame;
                    if (straightDimFrame(c, frame)) {
                        constexpr float kExtensionThickness = 1.0f;
                        drawList->AddLine(
                            Im2D::Vec2(frame.screenA.x(), frame.screenA.y()),
                            Im2D::Vec2(trackAx, trackAy),
                            arrowCol,
                            kExtensionThickness
                        );
                        drawList->AddLine(
                            Im2D::Vec2(frame.screenB.x(), frame.screenB.y()),
                            Im2D::Vec2(trackBx, trackBy),
                            arrowCol,
                            kExtensionThickness
                        );
                    }
                    const float dx = trackBx - trackAx;
                    const float dy = trackBy - trackAy;
                    const float len = std::sqrt(dx * dx + dy * dy);
                    if (len > 4.0f) {
                        const float angleDeg = -std::atan2(dy, dx) * 180.0f / pi;
                        Im2D::AddDoubleArrow(*drawList, 
                            Im2D::Transform(trackAx, trackAy, angleDeg),
                            arrowCol,
                            len,
                            thickness
                        );
                    }
                    // Mark both arrows while the line is under the cursor: the whole
                    // line is the grip that moves the dimension, and nothing else on
                    // screen says so.
                    if (i == m_labelHover || i == m_labelDrag) {
                        const LabelHandle handle
                            = i == m_labelDrag ? m_labelDragHandle : m_labelHoverHandle;
                        if (handle == LabelHandle::DimensionLine) {
                            drawList->AddCircleFilled(
                                Im2D::Vec2(trackAx, trackAy), 4.5f, Im2D::ColorU32(255, 255, 140, 235));
                            drawList->AddCircleFilled(
                                Im2D::Vec2(trackBx, trackBy), 4.5f, Im2D::ColorU32(255, 255, 140, 235));
                        }
                    }
                }
            }
            else if (c->Type == Sketcher::ConstraintType::Radius
                || c->Type == Sketcher::ConstraintType::Diameter) {
                // The radial shaft runs from the centre to a point of the rim; both
                // ends come out of radiusDimShaft already projected, so they stay on
                // the centre and on the rim however the camera is turned.
                Eigen::Vector2f centerS;
                Eigen::Vector2f rimS;
                if (radiusDimShaft(c, centerS, rimS)) {
                    const float rx = rimS.x() - centerS.x();
                    const float ry = rimS.y() - centerS.y();
                    const float rimDist = std::sqrt(rx * rx + ry * ry);
                    if (rimDist > 4.0f) {
                        // The angle is measured the other way round because the screen
                        // y axis points down.
                        const float angleDeg = -std::atan2(ry, rx) * 180.0f / pi;
                        if (c->Type == Sketcher::ConstraintType::Diameter) {
                            // A diameter runs from one rim through the centre to the
                            // other, so it carries an arrow at either end.
                            Im2D::AddDoubleArrow(*drawList, 
                                Im2D::Transform(
                                    centerS.x() - rx, centerS.y() - ry, angleDeg
                                ),
                                arrowCol,
                                rimDist * 2.0f,
                                thickness
                            );
                        }
                        else {
                            // A radius leaves the centre and ends on the rim, so only
                            // that end carries an arrow. AddArrow puts its tip one head
                            // length past the end of the line, so the line stops that
                            // much short of the rim and the tip lands on it.
                            const float head = 2.0f * 2.5f * thickness;
                            Im2D::AddArrow(*drawList, 
                                Im2D::Transform(centerS.x(), centerS.y(), angleDeg),
                                arrowCol,
                                std::max(rimDist - head, 1.0f),
                                thickness
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
                    Im2D::AddDoubleArcArrow(*drawList, 
                        Im2D::Transform(cx, cy, startDeg),
                        arrowCol,
                        arcRadius,
                        sweepDeg,
                        thickness
                    );
                    // Mark the arc while the cursor is on it: it is the grip that pulls
                    // the annotation in and out, and nothing else says so.
                    if ((i == m_labelHover || i == m_labelDrag)
                        && ((i == m_labelDrag ? m_labelDragHandle : m_labelHoverHandle)
                            == LabelHandle::AngleArc)) {
                        const float midDeg = startDeg + sweepDeg * 0.5f;
                        const float midRad = midDeg * pi / 180.0f;
                        drawList->AddCircleFilled(
                            Im2D::Vec2(
                                cx + std::cos(midRad) * arcRadius,
                                cy + std::sin(midRad) * arcRadius
                            ),
                            4.5f,
                            Im2D::ColorU32(255, 255, 140, 235)
                        );
                    }
                }
            }
            const Im2D::ImU32 textCol = isError ? Im2D::ColorU32(255, 92, 92, 255)
                : abgrToImU32(m_drawOption.constraintColor);
            const Im2D::ImU32 borderCol = isError ? Im2D::ColorU32(255, 120, 120, 220)
                : hovered ? Im2D::ColorU32(255, 255, 140, 255) : Im2D::ColorU32(255, 255, 255, 42);
            //drawList->AddRectFilled(boxMin, boxMax, Im2D::ColorU32(24, 24, 30, 178), 4.0f);
            //drawList->AddRect(boxMin, boxMax, borderCol, 4.0f);
            // The label is anchored to a point on the sketch plane and nudged by the same
            // screen offset the 2D box used, so it follows the model instead of the window.
            // Drawing it through the 3D text pass keeps annotations and geometry in one space.
            const Im2D::ImU32 rawCol = textCol;
            const Eigen::Vector4<uint8_t> labelRgba(
                static_cast<uint8_t>((rawCol >> 16) & 0xFF),
                static_cast<uint8_t>((rawCol >> 8) & 0xFF),
                static_cast<uint8_t>(rawCol & 0xFF),
                static_cast<uint8_t>((rawCol >> 24) & 0xFF));
            const float ascent = AnnotationFont().GetAscent(kAnnotationFontSize);
            const Eigen::Vector3f labelAnchor = m_sketch->plane().valueEigen(anchor);
            const auto& sketchPlane = m_sketch->plane();
            const Eigen::Vector3f planeRight(
                static_cast<float>(sketchPlane.xAxis.x),
                static_cast<float>(sketchPlane.xAxis.y),
                static_cast<float>(sketchPlane.xAxis.z));
            const Eigen::Vector3f planeUp(
                static_cast<float>(sketchPlane.yAxis.x),
                static_cast<float>(sketchPlane.yAxis.y),
                static_cast<float>(sketchPlane.yAxis.z));
            // The label centre (sx, sy) carries the anchor offset and the user's drag; move it
            // into pixels relative to the anchor so the 3D text lands exactly where the 2D one
            // used to be.
            const Eigen::Vector2f anchorScreen = renderer->worldToScreen(labelAnchor);
            const float labelOffX = sx - anchorScreen.x();
            const float labelOffY = sy - anchorScreen.y();
            renderer->drawLabel3D(labelAnchor, text.c_str(), kAnnotationFontSize, labelRgba,
                labelOffX - ts.x * 0.5f, labelOffY - ts.y * 0.5f + ascent, planeRight, planeUp, true);
        }
    }
    void SketcherObjWidget::editConstraintValue(int constrId)
    {
        const Sketcher::Constraint* c = m_sketch->getConstraint(constrId);
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
        const int err = m_sketch->setDatum(constrId, datum);
        if (err != 0) {
            CORE_ERROR("Constraint datum change failed, solver error code {}", err);
        }
    }
    bool SketcherObjWidget::computeTangentIconAnchor(
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
            const Part::Geometry* geo = m_sketch->resolveGeometry(geoId);
            if (!geo) {
                return false;
            }
            if (geo->is<Part::GeomLineSegment>()) {
                Base::Vector2d s, e;
                if (m_sketch->getGeometryPointSketch(geoId, PointPos::start, s)
                    && m_sketch->getGeometryPointSketch(geoId, PointPos::end, e)) {
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
            if (m_sketch->getGeometryPointSketch(constraint->First, constraint->FirstPos, anchorSketch)
                && tangentDirAt(constraint->First, anchorSketch, dirSketch)) {
                Base::Vector2d center;
                double radius = 0.0;
                if (getCircleArcInfo(m_sketch->resolveGeometry(constraint->First), center, radius)
                    || getCircleArcInfo(m_sketch->resolveGeometry(constraint->Second), center, radius)) {
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
            if (m_sketch->getGeometryPointSketch(constraint->Second, constraint->SecondPos, anchorSketch)
                && tangentDirAt(constraint->Second, anchorSketch, dirSketch)) {
                Base::Vector2d center;
                double radius = 0.0;
                if (getCircleArcInfo(m_sketch->resolveGeometry(constraint->First), center, radius)
                    || getCircleArcInfo(m_sketch->resolveGeometry(constraint->Second), center, radius)) {
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

        const Part::Geometry* g1 = m_sketch->resolveGeometry(constraint->First);
        const Part::Geometry* g2 = m_sketch->resolveGeometry(constraint->Second);
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
                if (m_sketch->getGeometryPointSketch(lineId, PointPos::start, l0)
                    && m_sketch->getGeometryPointSketch(lineId, PointPos::end, l1)) {
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
    void SketcherObjWidget::drawTangentIcons()
    {
        if (!InEdit()) {
            return;
        }
        Im2D::DrawList* drawList = &Rendering::Overlay::Im2DRender::Instance().GetDrawList();
        if (!drawList) {
            return;
        }
        for (int i = 0; i < static_cast<int>(m_sketch->constraints().size()); ++i) {
            const Sketcher::Constraint* c = m_sketch->constraints()[i];
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
            const Im2D::ImU32 col = isError ? Im2D::ColorU32(255, 110, 110, 255)
                                      : abgrToImU32(m_drawOption.constraintColor);
            const auto screenOfSketch = [this](const Base::Vector2d& sk) {
                const Base::Vector3d w = m_sketch->plane().origin + sk.x * m_sketch->plane().xAxis
                    + sk.y * m_sketch->plane().yAxis;
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
            drawList->AddCircle(Im2D::Vec2(cx, cy), circleR, col, 0, 2.0f);
            drawList->AddLine(Im2D::Vec2(lx0, ly0), Im2D::Vec2(lx1, ly1), col, 2.0f);
        }
    }
    void SketcherObjWidget::drawConstraintIcons()
    {
        if (!InEdit()) {
            return;
        }
        Im2D::DrawList* drawList = &Rendering::Overlay::Im2DRender::Instance().GetDrawList();
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
                return m_sketch->getGeometryPointSketch(geoId, pos, out);
            }
            return m_sketch->getGeometryCenterSketch(geoId, out);
        };
        const auto screenOf = [this](const Base::Vector2d& sk) {
            return renderer->worldToScreen(m_sketch->plane().valueEigen(sk));
        };

        for (int i = 0; i < static_cast<int>(m_sketch->constraints().size()); ++i) {
            const Sketcher::Constraint* c = m_sketch->constraints()[i];
            if (!c || !c->isVisible || c->isDimensional()) {
                continue;
            }
            const bool isError = constraintInError(i);
            const Im2D::ImU32 col = isError ? Im2D::ColorU32(255, 110, 110, 255)
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
                    const Im2D::Vec2 centre(s.x() + 16.0f, s.y() + 16.0f);
                    drawList->AddCircle(centre, 10.0f, col, 0, 1.8f);
                    drawList->AddCircleFilled(Im2D::Vec2(centre.x - 3.0f, centre.y), 2.5f, col);
                    drawList->AddCircleFilled(Im2D::Vec2(centre.x + 3.0f, centre.y), 2.5f, col);
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
                    const Im2D::Vec2 centre(s.x() - 5.0f, s.y() - 5.0f);
                    const float radius = 20.0f;
                    constexpr float kPi = 3.14159265358979f;
                    const int seg = 8;
                    Im2D::Vec2 arc[seg + 1];
                    for (int k = 0; k <= seg; ++k) {
                        const float deg = 180.0f + 90.0f * static_cast<float>(k) / seg;
                        const float a0 = deg * kPi / 180.0f;
                        arc[k] = Im2D::Vec2(
                            centre.x + radius * std::cos(a0),
                            centre.y + radius * std::sin(a0)
                        );
                    }
                    drawList->AddPolyline(arc, seg + 1, col, 1.8f);
                    const float midDeg = 225.0f * kPi / 180.0f;
                    drawList->AddCircleFilled(
                        Im2D::Vec2(centre.x + radius * std::cos(midDeg),
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
                            Im2D::Vec2(s.x() - halfLen, s.y() + offsetY),
                            Im2D::Vec2(s.x() + halfLen, s.y() + offsetY),
                            col,
                            2.4f
                        );
                        drawList->AddText(
                            AnnotationFont(),
                            kAnnotationFontSize * 1.4f,
                            Im2D::Vec2(s.x() + halfLen + 5.0f, s.y() + offsetY - 9.0f),
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
                            Im2D::Vec2(s.x() + offsetX, s.y() - lineHalf),
                            Im2D::Vec2(s.x() + offsetX, s.y() + lineHalf),
                            col,
                            3.0f
                        );
                        drawList->AddText(
                            AnnotationFont(),
                            kAnnotationFontSize * 1.4f,
                            Im2D::Vec2(s.x() + offsetX + lineHalf + 5.0f, s.y() - 9.0f),
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
                        Im2D::Vec2(s.x() - 9.0f, s.y() - 3.0f),
                        Im2D::Vec2(s.x() + 9.0f, s.y() - 3.0f),
                        col,
                        1.5f
                    );
                    drawList->AddLine(
                        Im2D::Vec2(s.x() - 9.0f, s.y() + 3.0f),
                        Im2D::Vec2(s.x() + 9.0f, s.y() + 3.0f),
                        col,
                        1.5f
                    );
                    drawList->AddText(
                        AnnotationFont(), kAnnotationFontSize,
                        Im2D::Vec2(s.x() + 11.0f, s.y() - 8.0f),
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
                    if (m_sketch->getGeometryPointSketch(c->First, PointPos::start, p1)
                        && m_sketch->getGeometryPointSketch(c->First, PointPos::end, p2)) {
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
                            Im2D::Vec2(s.x() + dS.x() * half + nx * sep,
                                   s.y() + dS.y() * half + ny * sep),
                            Im2D::Vec2(s.x() - dS.x() * half + nx * sep,
                                   s.y() - dS.y() * half + ny * sep),
                            col,
                            2.2f
                        );
                        drawList->AddLine(
                            Im2D::Vec2(s.x() + dS.x() * half - nx * sep,
                                   s.y() + dS.y() * half - ny * sep),
                            Im2D::Vec2(s.x() - dS.x() * half - nx * sep,
                                   s.y() - dS.y() * half - ny * sep),
                            col,
                            2.2f
                        );
                        drawList->AddText(
                            AnnotationFont(),
                            kAnnotationFontSize * 1.4f,
                            Im2D::Vec2(s.x() + dS.x() * (half + 7.0f) + nx * sep,
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
                        Im2D::Vec2(s.x(), s.y()),
                        Im2D::Vec2(s.x() + leg, s.y()),
                        col,
                        thick
                    );
                    drawList->AddLine(
                        Im2D::Vec2(s.x() + leg, s.y()),
                        Im2D::Vec2(s.x() + leg, s.y() + leg),
                        col,
                        thick
                    );
                    drawList->AddText(
                        AnnotationFont(),
                        kAnnotationFontSize * 1.4f,
                        Im2D::Vec2(s.x() + leg + 5.0f, s.y() - 9.0f),
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
                        Im2D::Vec2(s.x(), s.y() - 10.0f),
                        Im2D::Vec2(s.x(), s.y() + 10.0f),
                        col,
                        1.5f
                    );
                    drawList->AddCircleFilled(Im2D::Vec2(s.x() - 5.0f, s.y()), 2.0f, col);
                    drawList->AddCircleFilled(Im2D::Vec2(s.x() + 5.0f, s.y()), 2.0f, col);
                }
                break;
            }
            case Sketcher::ConstraintType::Block: {
                Base::Vector2d a;
                if (anchorOf(c, 0, a)) {
                    const Eigen::Vector2f s = screenOf(a);
                    drawList->AddRect(
                        Im2D::Vec2(s.x() - 8.0f, s.y() - 8.0f),
                        Im2D::Vec2(s.x() + 8.0f, s.y() + 8.0f),
                        col,
                        0.0f,
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









