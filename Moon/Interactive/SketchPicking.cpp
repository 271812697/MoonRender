#include "Interactive/SketchPicking.h"

#include "Interactive/Im3DRenderer.h"
#include "renderer/SceneView.h"

#include <cmath>

namespace MOON {
	namespace SketchPicking {
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

    int pickGeoIndex(SketcherObj& sketch, const Base::Vector2d& pos, const Base::Matrix4D& mat)
    {

        Base::Matrix4D trans = mat * sketch.getplaneTransform();
        Base::Vector3d p1 = trans * Base::Vector3d(pos.x, pos.y, 0);

        int ret = -1;
        double deltaTole = 15.0;
        double minDist = 10000.0;
        // 遍历所有几何图元
        for (int i = 0; i < sketch.geometries().size(); i++) {
            if (!sketch.isGeometryVisible(i)) {
                continue;  // hidden geometry is not pickable
            }
            Part::Geometry* geo = sketch.geometries()[i].get();
            if (geo->isDerivedFrom<Part::GeomCurve>()) {
                auto& segment = sketch.segmentOf(geo);
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

    SelectGeoId testSelect(SketcherObj& sketch, Editor::Panels::SceneView& view, const Base::Vector2d& pos)
    {
        Maths::FMatrix4 mat = view.GetCamera()->GetViewPortMatrix();
        Base::Matrix4D viewPortMat(
            mat.data[0], mat.data[1], mat.data[2], mat.data[3],
            mat.data[4], mat.data[5], mat.data[6], mat.data[7],
            mat.data[8], mat.data[9], mat.data[10], mat.data[11],
            mat.data[12], mat.data[13], mat.data[14], mat.data[15]
        );
        Base::Matrix4D trans = viewPortMat * sketch.getplaneTransform();
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
        candidates.reserve(sketch.geometries().size() + sketch.getExternalCurveCount());
        for (int i = 0; i < static_cast<int>(sketch.geometries().size()); ++i) {
            if (!sketch.isGeometryVisible(i)) {
                continue;
            }
            candidates.emplace_back(i, sketch.geometries()[i].get());
        }
        for (int i = 0; i < sketch.getExternalCurveCount(); ++i) {
            Part::Geometry* geo = const_cast<Part::Geometry*>(
                sketch.getExternalCurve(sketch.getExternalGeoId(i)));
            // Only a curve that has been sampled can be hit; a missing cache entry
            // must not be turned into an empty one here.
            if (geo != nullptr && sketch.findSegment(geo) != nullptr) {
                candidates.emplace_back(sketch.getExternalGeoId(i), geo);
            }
        }

        // The points of every curve first: an endpoint or a centre is what a constraint
        // usually wants, and it is a smaller target than the curve it belongs to.
        for (const auto& [geoId, geo] : candidates) {
            if (sketch.isAxisCurve(geoId)) {
                // The axes are not picked by their points: the only one that is a
                // feature of its own is the root point, and the origin test above has
                // already taken it. Their defining segment reaches from the origin to a
                // far end that means nothing, and offering that end as a pick would
                // hand the user a point that is not drawn anywhere.
                continue;
            }
            auto& segment = sketch.segmentOf(geo);
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
                if (sketch.isAxisCurve(geoId)) {
                    // The axes are tried after every curve, see below.
                    continue;
                }
                auto& segment = sketch.segmentOf(geo);
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
                if (!sketch.isAxisCurve(geoId)) {
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

    bool snapPoint(SketcherObj& sketch, Editor::Panels::SceneView& view, ImRenderer& renderer, Base::Vector2d& pos, const std::set<int>& avoid)
    {
        Maths::FMatrix4 mat = view.GetCamera()->GetViewPortMatrix();
        Base::Matrix4D pla(
            mat.data[0], mat.data[1], mat.data[2], mat.data[3],
            mat.data[4], mat.data[5], mat.data[6], mat.data[7],
            mat.data[8], mat.data[9], mat.data[10], mat.data[11],
            mat.data[12], mat.data[13], mat.data[14], mat.data[15]
        );
        Base::Matrix4D trans = pla * sketch.getplaneTransform();
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
            for (int i = 0; i < sketch.getExternalCurveCount(); ++i) {
                const int geoId = sketch.getExternalGeoId(i);
                if (avoid.count(geoId)) {
                    continue;
                }
                Part::Geometry* geo = const_cast<Part::Geometry*>(sketch.getExternalCurve(geoId));
                if (geo == nullptr) {
                    continue;
                }
                const CurveSegment* segment = sketch.findSegment(geo);
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
        for (int i = 0; i < sketch.geometries().size(); i++) {
            if (!avoid.count(i)) {
                if (sketch.isConstructionGeometry(i)) {
                    continue;  // construction aids are not snap targets
                }
                if (!sketch.isGeometryVisible(i)) {
                    continue;  // hidden geometry is not a snap target
                }
                Part::Geometry* geo = sketch.geometries()[i].get();
                auto& segment = sketch.segmentOf(geo);
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
            for (int i = 0; i < sketch.geometries().size(); i++) {
                if (!avoid.count(i)) {
                    if (sketch.isConstructionGeometry(i)) {
                        continue;
                    }
                    if (!sketch.isGeometryVisible(i)) {
                        continue;
                    }
                    Part::Geometry* geo = sketch.geometries()[i].get();
                    auto& segment = sketch.segmentOf(geo);
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
            for (int i = 0; i < sketch.getExternalCurveCount(); ++i) {
                const int geoId = sketch.getExternalGeoId(i);
                if (avoid.count(geoId)) {
                    continue;
                }
                Part::Geometry* geo = const_cast<Part::Geometry*>(sketch.getExternalCurve(geoId));
                if (geo == nullptr || !geo->isDerivedFrom<Part::GeomCurve>()) {
                    continue;
                }
                const CurveSegment* segment = sketch.findSegment(geo);
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
            ret = snapToGridPoint(sketch, view, renderer, pos);
        }
        return ret;
    }

    bool gridView(SketcherObj& sketch, Editor::Panels::SceneView& view, GridView& p_out)
    {
        const auto* camera = view.GetCamera();
        if (camera == nullptr) {
            return false;
        }
        const auto& proj = camera->GetProjectionMatrix();
        const float proj11 = proj(1, 1);
        const auto& fd = view.GetRenderer().GetFrameDescriptor();
        const float screenH = static_cast<float>(fd.renderHeight);
        const float screenW = static_cast<float>(fd.renderWidth);
        if (proj11 <= 0.0f || screenH <= 0.0f || screenW <= 0.0f
            || camera->GetProjectionMode()
                != ::Rendering::Settings::EProjectionMode::ORTHOGRAPHIC) {
            return false;
        }
        // The viewport rectangle in view space: an orthographic projection keeps only
        // x and y, so those two coordinates are all a plane point is judged by.
        const float halfH = 1.0f / proj11;

        // The sketch plane as the camera sees it: the view-space position of its origin
        // and the view-space direction its axes run in, so that the plane point (u, v)
        // sits at
        //     view(u, v) = (oX + u * uX + v * vX, oY + u * uY + v * vY).
        // The camera's view matrix, under its own name (view is the scene view).
        const auto& viewM = camera->GetViewMatrix();
        const auto viewOf = [&viewM](const Base::Vector3d& p_world) {
            return viewM.MulPoint(Maths::FVector3(
                static_cast<float>(p_world.x),
                static_cast<float>(p_world.y),
                static_cast<float>(p_world.z)));
            };
        const Maths::FVector3 viewOrigin = viewOf(sketch.plane().origin);
        const Maths::FVector3 viewX = viewOf(sketch.plane().origin + sketch.plane().xAxis);
        const Maths::FVector3 viewY = viewOf(sketch.plane().origin + sketch.plane().yAxis);
        const float uX = viewX.x - viewOrigin.x;
        const float uY = viewX.y - viewOrigin.y;
        const float vX = viewY.x - viewOrigin.x;
        const float vY = viewY.y - viewOrigin.y;
        // Seen along the plane there is nothing to describe: both of the plane's
        // directions are the view direction then (the plane is a line on screen, and
        // the visible part of it runs off to infinity).
        if (std::abs(uX * vY - vX * uY) < 1.0e-9f) {
            return false;
        }

        p_out.oX = viewOrigin.x;
        p_out.oY = viewOrigin.y;
        p_out.uX = uX;
        p_out.uY = uY;
        p_out.vX = vX;
        p_out.vY = vY;
        p_out.hx = halfH * screenW / screenH;
        p_out.hy = halfH;

        // How big one sketch unit is on screen: one unit along an axis covers that
        // axis' view-space length, and the viewport is 2 * halfH high. Face on both
        // lengths are 1, which is the plain zoom-based spacing; tilted, the axes are
        // foreshortened by different amounts, and their geometric mean keeps the step
        // near the 40 px target in both directions instead of letting one of them pile
        // its lines up on screen.
        const float scaleU = std::sqrt(uX * uX + uY * uY);
        const float scaleV = std::sqrt(vX * vX + vY * vY);
        const float scale = std::max(std::sqrt(scaleU * scaleV), 1.0e-6f);
        const float targetStep = 40.0f * 2.0f * halfH / (screenH * scale);
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
        p_out.step = step;
        return true;
    }

    bool snapToGridPoint(SketcherObj& sketch, Editor::Panels::SceneView& view, ImRenderer& renderer, Base::Vector2d& pos)
    {
        if (!sketch.isSnapToGrid() || !sketch.isDrawGrid()) {
            return false;
        }
        // The very step drawBackground() draws its lattice with: the cursor has to
        // land on the intersections the user sees, whatever the camera does.
        GridView grid;
        if (!gridView(sketch, view, grid)) {
            return false;
        }
        const float step = grid.step;
        const double gx = std::round(pos.x / step) * step;
        const double gy = std::round(pos.y / step) * step;
        const auto worldOf = [&sketch](const Base::Vector2d& sk) {
            return sketch.plane().origin + sk.x * sketch.plane().xAxis + sk.y * sketch.plane().yAxis;
        };
        const auto screenOf = [&worldOf, &renderer](const Base::Vector2d& sk) {
            const Base::Vector3d w = worldOf(sk);
            return renderer.worldToScreen(
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
	}
}
