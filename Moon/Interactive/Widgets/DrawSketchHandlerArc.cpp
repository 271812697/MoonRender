#include "Interactive/Widgets/DrawSketchHandlerArc.h"
#include "Interactive/Im3DRenderer.h"
#include "renderer/SceneView.h"
#include <ThirdParty/imgui/imgui/imgui.h>
#include "Interactive/Interactive/Event.h"
#include "Interactive/Interactive/ExecuteCommand.h"
#include "Interactive/Interactive/WidgetCallbackMapper.h"
#include "Interactive/Interactive/WidgetEvent.h"
#include "Interactive/Interactive/WidgetEventTranslator.h"
#include "Interactive/Interactive/RenderWindowInteractor.h"
#include <cmath>

namespace MOON {
    namespace
    {
        /** The angle boxes of the tool show degrees, the geometry works in radians. */
        constexpr double kDegreesPerRadian = 57.29577951308232;

        double toDegrees(double p_radians)
        {
            return p_radians * kDegreesPerRadian;
        }

        double toRadians(double p_degrees)
        {
            return p_degrees / kDegreesPerRadian;
        }
    }

    static Base::Vector2d getCircleCenter(
        const Base::Vector2d& p1,
        const Base::Vector2d& p2,
        const Base::Vector2d& p3
    )
    {
        Base::Vector2d u = p2 - p1;
        Base::Vector2d v = p3 - p2;
        Base::Vector2d w = p1 - p3;

        double uu = u * u;
        double vv = v * v;
        double ww = w * w;

        double eps2 = Precision::SquareConfusion();
        if (uu < eps2 || vv < eps2 || ww < eps2) {
            //THROWM(Base::ValueError, "Two points are coincident");
        }

        double uv = -(u * v);
        double vw = -(v * w);
        double uw = -(u * w);

        double w0 = (2 * sqrt(abs(uu * ww - uw * uw)) * uw / (uu * ww));
        double w1 = (2 * sqrt(abs(uu * vv - uv * uv)) * uv / (uu * vv));
        double w2 = (2 * sqrt(abs(vv * ww - vw * vw)) * vw / (vv * ww));

        double wx = w0 + w1 + w2;

        if (abs(wx) < Precision::Confusion()) {
            //THROWM(Base::ValueError, "Points are collinear");
        }

        double x = (w0 * p1.x + w1 * p2.x + w2 * p3.x) / wx;
        double y = (w0 * p1.y + w1 * p2.y + w2 * p3.y) / wx;

        return { x, y };
    }
    static bool areCollinear(
        const Base::Vector2d& p1,
        const Base::Vector2d& p2,
        const Base::Vector2d& p3
    )
    {
        Base::Vector2d u = p2 - p1;
        Base::Vector2d v = p3 - p2;
        Base::Vector2d w = p1 - p3;

        double uu = u * u;
        double vv = v * v;
        double ww = w * w;

        double eps2 = Precision::SquareConfusion();
        if (uu < eps2 || vv < eps2 || ww < eps2) {
            return true;
        }

        double uv = -(u * v);
        double vw = -(v * w);
        double uw = -(u * w);

        double w0 = (2 * sqrt(abs(uu * ww - uw * uw)) * uw / (uu * ww));
        double w1 = (2 * sqrt(abs(uu * vv - uv * uv)) * uv / (uu * vv));
        double w2 = (2 * sqrt(abs(vv * ww - vw * vw)) * vw / (vv * ww));

        double wx = w0 + w1 + w2;

        if (abs(wx) < Precision::Confusion()) {
            return true;
        }

        return false;
    }

    static Base::Vector3d toVector3d(const Base::Vector2d& vector2d)
    {
        return Base::Vector3d(vector2d.x, vector2d.y, 0.);
    }
	class DrawSketchHandlerArc::DrawSketchHandlerArcInternal {
	public:
        DrawSketchHandlerArcInternal(DrawSketchHandlerArc*s) :
            self(s), 
            radius(0.0)
            , startAngle(0.0)
            , endAngle(0.0)
            , arcAngle(0.0) {
		}
		~DrawSketchHandlerArcInternal() {
		
		}
	private:
		friend DrawSketchHandlerArc;
        DrawSketchHandlerArc* self = nullptr;
        // thirdPoint is the point the arc is being swept to: the tool only passes it
        // through on its way to the centre and the angles, but the "x3"/"y3" boxes of the
        // three-rim method have to show it.
        Base::Vector2d centerPoint, firstPoint, secondPoint, thirdPoint;
        double radius, startAngle, endAngle, arcAngle;
	};
	
    DrawSketchHandlerArc::DrawSketchHandlerArc(const std::string& name) :DrawSketchDefaultHandler<DrawSketchHandlerArc, StateMachines::ThreeSeekEnd, 3, CircleEllipseConstructionMethod>(name),m_internal(new DrawSketchHandlerArcInternal(this))
	{

	}

    DrawSketchHandlerArc::~DrawSketchHandlerArc()
	{
		delete m_internal;
	}

	void DrawSketchHandlerArc::onUpdate()
	{
        DrawSketchHandler::onUpdate();

        // The boxes follow the geometry every frame - the camera may have moved, and the
        // values they show are the ones the tool is about to use.
        updateOnViewParameterPositions();
        drawOnViewParameters();
	}

	void DrawSketchHandlerArc::onSetActive(bool flag)
	{
        if (flag) {
            configureOnViewParameters();
        }
        else {
            onViewParameters.clear();
        }
	}

    void DrawSketchHandlerArc::onConstructionMethodChanged()
    {
        // The two methods are given different numbers (a centre and two angles, or three
        // points on the rim), so the boxes are thrown away and made again.
        configureOnViewParameters();
    }

    void DrawSketchHandlerArc::configureOnViewParameters()
    {
        onViewParameters.clear();

        // Where the arc starts - the centre, or the first rim point - is placed with the
        // mouse; the boxes begin with the numbers that come after it.
        if (constructionMethod() == ConstructionMethod::Center) {
            // How big the arc is and where it starts, then how far it sweeps from there.
            onViewParameters.add("radius");
            onViewParameters.add("start");
            onViewParameters.add("sweep");
        }
        else {
            // The two further points the arc goes through.
            onViewParameters.add("x2");
            onViewParameters.add("y2");
            onViewParameters.add("x3");
            onViewParameters.add("y3");
        }

        // Typing in a box redoes the geometry right away, as if the mouse had been moved
        // to where the number puts it (the enforcement below is what does the placing).
        onViewParameters.onValueChanged = [this](int, double) {
            redrawFromControlParameters();
        };
        // A confirmed number says the step it belongs to is answered; with all of them the
        // tool goes on by itself (see advanceOnViewParametersIfComplete).
        onViewParameters.onEditingFinished = [this](int index, double value) {
            redrawFromControlParameters();
            finishOnViewParameterStep(index, value);
        };
        onViewParameters.onReleased = [this](int) {
            // The parameter is the mouse's again: let it take over where the cursor is.
            redrawFromControlParameters();
        };
    }

    void DrawSketchHandlerArc::redrawFromControlParameters()
    {
        Base::Vector2d pos = onSketchPos;
        enforceControlParameters(pos);
        updateDataAndDrawToPosition(pos);
        adaptParameters(pos);
    }

    void DrawSketchHandlerArc::setValueIfUnset(int p_index, double p_value)
    {
        OnViewParameter* parameter = onViewParameters.at(p_index);
        if (parameter != nullptr && !parameter->isSet()) {
            parameter->setValue(p_value);
        }
    }

    StateMachines::ThreeSeekEnd DrawSketchHandlerArc::stateOfOnViewParameter(int p_index) const
    {
        // The numbers come after the point the step before placed: the radius and the
        // starting angle, then the sweep (or the second and third rim points).
        if (p_index < 2) {
            return SelectMode::SeekSecond;
        }
        return SelectMode::SeekThird;
    }

    void DrawSketchHandlerArc::adaptParameters(Base::Vector2d /*onSketchPos*/)
    {
        const int expectedBoxes = constructionMethod() == ConstructionMethod::Center ? 3 : 4;
        if (onViewParameters.size() < expectedBoxes) {
            return;  // the tool is not active: there are no boxes to write
        }
        // What the mouse (or a typed value, which wins) has made of the tool so far, shown
        // in the boxes the user has not filled in himself.
        const bool isCenter = constructionMethod() == ConstructionMethod::Center;

        if (state() == SelectMode::SeekSecond) {
            if (isCenter) {
                setValueIfUnset(0, m_internal->radius);
                setValueIfUnset(1, toDegrees(m_internal->startAngle));
            }
            else {
                setValueIfUnset(0, m_internal->secondPoint.x);
                setValueIfUnset(1, m_internal->secondPoint.y);
            }
        }
        else if (state() == SelectMode::SeekThird) {
            if (isCenter) {
                setValueIfUnset(2, toDegrees(m_internal->arcAngle));
            }
            else {
                setValueIfUnset(2, m_internal->thirdPoint.x);
                setValueIfUnset(3, m_internal->thirdPoint.y);
            }
        }
    }

    void DrawSketchHandlerArc::enforceControlParameters(Base::Vector2d& onSketchPos)
    {
        const int expectedBoxes = constructionMethod() == ConstructionMethod::Center ? 3 : 4;
        if (onViewParameters.size() < expectedBoxes) {
            return;  // the tool is not active: there are no boxes to read
        }
        const bool isCenter = constructionMethod() == ConstructionMethod::Center;

        switch (state()) {
        case SelectMode::SeekSecond: {
            OnViewParameter* radiusBox = onViewParameters.at(0);
            OnViewParameter* angleBox = onViewParameters.at(1);
            if (isCenter) {
                // The centre is known: the radius and the starting angle decide where the
                // cursor is taken to, and the direction the mouse points at is kept for
                // whichever of the two was not typed in.
                Base::Vector2d dir = onSketchPos - m_internal->centerPoint;
                if (dir.Length() < Precision::Confusion()) {
                    dir.x = 1.0;
                }
                double radius = radiusBox->isSet() ? radiusBox->value() : dir.Length();
                if (radius < Precision::Confusion()) {
                    // A radius of nothing is not a radius: the mouse takes it back.
                    radiusBox->release();
                    return;
                }
                if (angleBox->isSet()) {
                    const double angle = toRadians(angleBox->value());
                    onSketchPos = m_internal->centerPoint
                        + radius * Base::Vector2d(cos(angle), sin(angle));
                }
                else {
                    onSketchPos = m_internal->centerPoint + radius * dir.Normalize();
                }
            }
            else {
                // The second of the three rim points: x and y on their own.
                if (radiusBox->isSet()) {
                    onSketchPos.x = radiusBox->value();
                }
                if (angleBox->isSet()) {
                    onSketchPos.y = angleBox->value();
                }
            }
        } break;
        case SelectMode::SeekThird: {
            OnViewParameter* sweepBox = onViewParameters.at(2);
            if (isCenter) {
                if (sweepBox->isSet()) {
                    const double sweep = toRadians(sweepBox->value());
                    if (std::fabs(sweep) < Precision::Confusion()) {
                        sweepBox->release();  // a sweep of nothing is not an arc
                        return;
                    }
                    // The tool keeps the sweep that is closest to the one it already had
                    // (it cannot tell which way round the user is going): giving it the
                    // typed one first is what makes the arc exactly as far as it says.
                    m_internal->arcAngle = sweep;
                    const double angle = m_internal->startAngle + sweep;
                    onSketchPos = m_internal->centerPoint
                        + m_internal->radius * Base::Vector2d(cos(angle), sin(angle));
                }
            }
            else {
                // The last of the three rim points: x and y on their own.
                if (sweepBox->isSet()) {
                    onSketchPos.x = sweepBox->value();
                }
                if (OnViewParameter* y = onViewParameters.at(3); y != nullptr && y->isSet()) {
                    onSketchPos.y = y->value();
                }
            }
        } break;
        default:
            break;
        }
    }

    void DrawSketchHandlerArc::updateOnViewParameterPositions()
    {
        adaptParameters(onSketchPos);

        if (m_sceneView == nullptr) {
            return;
        }

        // The boxes sit next to the point the step is about, stacked downwards so that the
        // two or three of them do not cover each other.
        Base::Vector2d anchor = m_internal->secondPoint;
        if (state() == SelectMode::SeekSecond) {
            anchor = constructionMethod() == ConstructionMethod::Center
                ? (m_internal->centerPoint + m_internal->firstPoint) / 2
                : m_internal->secondPoint;
        }
        else if (state() == SelectMode::SeekThird) {
            anchor = constructionMethod() == ConstructionMethod::Center
                ? (m_internal->centerPoint + m_internal->secondPoint) / 2
                : m_internal->thirdPoint;
        }

        const Maths::FVector2 screen = m_sceneView->worldToScreen(getWorldPosFromSketchPos(anchor));
        int slot = 0;
        for (int i = 0; i < onViewParameters.size(); i++) {
            if (stateOfOnViewParameter(i) != state()) {
                continue;
            }
            onViewParameters.at(i)->setScreenPosition(
                screen.x + 18.0f, screen.y - 12.0f + 24.0f * slot);
            slot++;
        }
    }





	void DrawSketchHandlerArc::updateDataAndDrawToPosition(Base::Vector2d onSketchPos)
	{
        switch (state()) {
        case SelectMode::SeekFirst: {
            //toolWidgetManager.drawPositionAtCursor(onSketchPos);

            if (constructionMethod() == ConstructionMethod::Center) {
                m_internal->centerPoint = onSketchPos;
            }
            else {
                m_internal->firstPoint = onSketchPos;
            }

            //seekAndRenderAutoConstraint(sugConstraints[0], onSketchPos, Base::Vector2d(0.f, 0.f));
        } break;
        case SelectMode::SeekSecond: {
            if (constructionMethod() == ConstructionMethod::Center) {
                m_internal->firstPoint = onSketchPos;
                m_internal->startAngle = (m_internal->firstPoint - m_internal->centerPoint).Angle();
            }
            else {
                m_internal->centerPoint = (onSketchPos - m_internal->firstPoint) / 2 + m_internal->firstPoint;
                m_internal->secondPoint = onSketchPos;
            }

            m_internal->radius = (onSketchPos - m_internal->centerPoint).Length();

            CreateAndDrawShapeGeometry();

            if (constructionMethod() == ConstructionMethod::Center) {
                //toolWidgetManager.drawDirectionAtCursor(onSketchPos, centerPoint);
            }
            else {
                //toolWidgetManager.drawPositionAtCursor(onSketchPos);
            }

            //seekAndRenderAutoConstraint(sugConstraints[1], onSketchPos, Base::Vector2d(0.f, 0.f));
        } break;
        case SelectMode::SeekThird: 
        {
            double startAngleBackup = m_internal->startAngle;

            if (constructionMethod() == ConstructionMethod::Center) {
                m_internal->secondPoint = onSketchPos;
                double angle1 = (onSketchPos - m_internal->centerPoint).Angle() - m_internal->startAngle;
                double angle2 = angle1 + (angle1 < 0. ? 2 : -2) * 3.1415926535;
                m_internal->arcAngle = abs(angle1 - m_internal->arcAngle) < abs(angle2 - m_internal->arcAngle) ? angle1 : angle2;

                if (m_internal->arcAngle > 0) {
                    m_internal->endAngle = m_internal->startAngle + m_internal->arcAngle;
                }
                else {
                    m_internal->endAngle = m_internal->startAngle;
                    m_internal->startAngle = m_internal->startAngle + m_internal->arcAngle;
                }
            }
            else {
                if (areCollinear(m_internal->firstPoint, m_internal->secondPoint, onSketchPos)) {
                    // If points are collinear then we can't calculate the center.
                    return;
                }
                m_internal->thirdPoint = onSketchPos;
                m_internal->centerPoint
                    = getCircleCenter(m_internal->firstPoint, m_internal->secondPoint, onSketchPos);
                m_internal->radius = (onSketchPos - m_internal->centerPoint).Length();

                double angle1 = (m_internal->firstPoint - m_internal->centerPoint).Angle();
                double angle2 = (m_internal->secondPoint - m_internal->centerPoint).Angle();
                double angle3 = (onSketchPos - m_internal->centerPoint).Angle();

                // Always build arc counter-clockwise
                // Point 3 is between Point 1 and 2
                if (angle3 > std::min(angle1, angle2) && angle3 < std::max(angle1, angle2)) {
                    //if (angle2 > angle1) {
                    //    arcPos1 = Sketcher::PointPos::start;
                    //    arcPos2 = Sketcher::PointPos::end;
                    //}
                    //else {
                    //    arcPos1 = Sketcher::PointPos::end;
                    //    arcPos2 = Sketcher::PointPos::start;
                    //}
                    m_internal->startAngle = std::min(angle1, angle2);
                    m_internal->endAngle = std::max(angle1, angle2);
                    m_internal->arcAngle = m_internal->endAngle - m_internal->startAngle;
                }
                // Point 3 is not between Point 1 and 2
                else {
                    //if (angle2 > angle1) {
                    //    arcPos1 = Sketcher::PointPos::end;
                    //    arcPos2 = Sketcher::PointPos::start;
                    //}
                    //else {
                    //    arcPos1 = Sketcher::PointPos::start;
                    //    arcPos2 = Sketcher::PointPos::end;
                    //}
                    m_internal->startAngle = std::max(angle1, angle2);
                    m_internal->endAngle = std::min(angle1, angle2);
                    m_internal->arcAngle = 2 * 3.1415926535 - (m_internal->startAngle - m_internal->endAngle);
                }
            }

            CreateAndDrawShapeGeometry();

            if (constructionMethod() == ConstructionMethod::Center) {
                m_internal->startAngle = startAngleBackup;
                //toolWidgetManager.drawDoubleAtCursor(onSketchPos, arcAngle, Base::Unit::Angle);
                //seekAndRenderAutoConstraint(sugConstraints[2], onSketchPos, Base::Vector2d(0.0, 0.0));
            }
            else {
                //toolWidgetManager.drawPositionAtCursor(onSketchPos);
                //seekAndRenderAutoConstraint(
                //    sugConstraints[2],
                //    onSketchPos,
                //    Base::Vector2d(0.f, 0.f),
                //    AutoConstraint::CURVE
                //);
            }

        } break;
        default:
            break;
        }
	}



    bool DrawSketchHandlerArc::canGoToNextMode()
    {
        if (state() == SelectMode::SeekSecond && m_internal->radius < Precision::Confusion()) {
            // Prevent validation of null arc.
            return false;
        }
        if (state() == SelectMode::SeekThird && fabs(m_internal->arcAngle) < Precision::Confusion()) {
            return false;
        }
        return true;
    }

    void DrawSketchHandlerArc::createShape(bool onlyeditoutline)
    {
        ShapeGeometry.clear();

        if (m_internal->radius < Precision::Confusion()) {
            return;
        }

        if (state() == SelectMode::SeekSecond) {
            addCircleToShapeGeometry(toVector3d(m_internal->centerPoint), m_internal->radius, false);
        }
        else {
            if (fabs(m_internal->arcAngle) < Precision::Confusion()) {
                return;
            }

            addArcToShapeGeometry(
                toVector3d(m_internal->centerPoint),
                m_internal->startAngle,
                m_internal->endAngle,
                m_internal->radius,
                false
            );
        }

        //if (onlyeditoutline)
        //{
        //    if (constructionMethod() == ConstructionMethod::Center) {
        //        if (state() == SelectMode::SeekThird) {
        //            const double scale = 0.8;
        //            addLineToShapeGeometry(
        //                toVector3d(m_internal->centerPoint),
        //                Base::Vector3d(
        //                    m_internal->centerPoint.x + cos(m_internal->startAngle) * scale * m_internal->radius,
        //                    m_internal->centerPoint.y + sin(m_internal->startAngle) * scale * m_internal->radius,
        //                    0.
        //                ),
        //                true
        //            );

        //            addLineToShapeGeometry(
        //                toVector3d(m_internal->centerPoint),
        //                Base::Vector3d(
        //                    m_internal->centerPoint.x + cos(m_internal->endAngle) * scale * m_internal->radius,
        //                    m_internal->centerPoint.y + sin(m_internal->endAngle) * scale * m_internal->radius,
        //                    0.
        //                ),
        //                true
        //            );
        //        }
        //    }
        //    else {
        //        if (state() == SelectMode::SeekSecond) {
        //            addLineToShapeGeometry(
        //                toVector3d(m_internal->firstPoint),
        //                toVector3d(m_internal->secondPoint),
        //                true
        //            );
        //        }
        //        else if (state() == SelectMode::SeekThird) {
        //            const double scale = 0.8;
        //            addLineToShapeGeometry(
        //                toVector3d(m_internal->centerPoint),
        //                toVector3d(m_internal->centerPoint)
        //                + (toVector3d(m_internal->secondPoint) - toVector3d(m_internal->centerPoint)) * scale,
        //                true
        //            );

        //            addLineToShapeGeometry(
        //                toVector3d(m_internal->centerPoint),
        //                toVector3d(m_internal->centerPoint)
        //                + (toVector3d(m_internal->firstPoint) - toVector3d(m_internal->centerPoint)) * scale,
        //                true
        //            );
        //        }
        //    }
        //}
    }

}
