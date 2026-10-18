#include "Interactive/Widgets/DrawSketchHandlerCircle.h"
#include "Interactive/Im3DRenderer.h"
#include "renderer/SceneView.h"
#include <ThirdParty/imgui/imgui/imgui.h>
#include "Interactive/Interactive/Event.h"
#include "Interactive/Interactive/ExecuteCommand.h"
#include "Interactive/Interactive/WidgetCallbackMapper.h"
#include "Interactive/Interactive/WidgetEvent.h"
#include "Interactive/Interactive/WidgetEventTranslator.h"
#include "Interactive/Interactive/RenderWindowInteractor.h"
#include "Geometry2d.h"

namespace MOON {
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
	class DrawSketchHandlerCircle::DrawSketchHandlerCircleInternal {
	public:
		DrawSketchHandlerCircleInternal(DrawSketchHandlerCircle*s) :self(s){
            centerPoint = Base::Vector2d{ 0.0,0.0 };
			radius = 5;
		}
		~DrawSketchHandlerCircleInternal() {
		
		}
	private:
		friend DrawSketchHandlerCircle;
		DrawSketchHandlerCircle* self = nullptr;
		
		float radius;
        // thirdPoint is the point the circle is being drawn through: the tool only passes
        // it through on its way to the centre, but the "x3"/"y3" boxes of the three rim
        // point method have to show it.
        Base::Vector2d centerPoint, firstPoint, secondPoint, thirdPoint;
       
        bool isDiameter;
	};
	
	DrawSketchHandlerCircle::DrawSketchHandlerCircle(const std::string& name) :DrawSketchDefaultHandler<DrawSketchHandlerCircle, StateMachines::ThreeSeekEnd, 3, CircleEllipseConstructionMethod>(name),m_internal(new DrawSketchHandlerCircleInternal(this))
	{

	}

	DrawSketchHandlerCircle::~DrawSketchHandlerCircle()
	{
		delete m_internal;
	}

	void DrawSketchHandlerCircle::onUpdate()
	{
        DrawSketchHandler::onUpdate();
        // The radius has a box of its own now (it used to be written at the cursor): the
        // boxes follow the geometry every frame - the camera may have moved - and show the
        // values the tool is about to use.
        updateOnViewParameterPositions();
        drawOnViewParameters();
	}

	void DrawSketchHandlerCircle::onSetActive(bool flag)
	{
        if (flag) {
            configureOnViewParameters();
        }
        else {
            onViewParameters.clear();
        }
	}

    void DrawSketchHandlerCircle::onConstructionMethodChanged()
    {
        // The two methods are given different numbers (a centre and a radius, or three
        // points on the rim), so the boxes are thrown away and made again.
        configureOnViewParameters();
    }

    void DrawSketchHandlerCircle::configureOnViewParameters()
    {
        onViewParameters.clear();

        // Where the circle starts - the centre, or the first point of the rim - is placed
        // with the mouse; the boxes begin with the numbers that come after it.
        if (constructionMethod() == ConstructionMethod::Center) {
            onViewParameters.add("radius");
        }
        else {
            onViewParameters.add("x2");
            onViewParameters.add("y2");
            onViewParameters.add("x3");
            onViewParameters.add("y3");
        }

        onViewParameters.onValueChanged = [this](int, double) {
            redrawFromControlParameters();
        };
        onViewParameters.onEditingFinished = [this](int index, double value) {
            redrawFromControlParameters();
            finishOnViewParameterStep(index, value);
        };
        onViewParameters.onReleased = [this](int) {
            redrawFromControlParameters();
        };
    }

    void DrawSketchHandlerCircle::redrawFromControlParameters()
    {
        Base::Vector2d pos = onSketchPos;
        enforceControlParameters(pos);
        updateDataAndDrawToPosition(pos);
        adaptParameters(pos);
    }

    void DrawSketchHandlerCircle::setValueIfUnset(int p_index, double p_value)
    {
        OnViewParameter* parameter = onViewParameters.at(p_index);
        if (parameter != nullptr && !parameter->isSet()) {
            parameter->setValue(p_value);
        }
    }

    StateMachines::ThreeSeekEnd DrawSketchHandlerCircle::stateOfOnViewParameter(int p_index) const
    {
        if (constructionMethod() == ConstructionMethod::Center) {
            // The radius, once the centre is placed - the method is done with those two.
            return SelectMode::SeekSecond;
        }
        // The second and third rim points, two numbers each (the first one is placed with
        // the mouse like the centre is).
        if (p_index < 2) {
            return SelectMode::SeekSecond;
        }
        return SelectMode::SeekThird;
    }

    bool DrawSketchHandlerCircle::isLastControlStep() const
    {
        return state() == SelectMode::SeekSecond
            && constructionMethod() == ConstructionMethod::Center;
    }

    void DrawSketchHandlerCircle::adaptParameters(Base::Vector2d /*onSketchPos*/)
    {
        const int expectedBoxes = constructionMethod() == ConstructionMethod::Center ? 1 : 4;
        if (onViewParameters.size() < expectedBoxes) {
            return;  // the tool is not active: there are no boxes to write
        }
        const bool isCenter = constructionMethod() == ConstructionMethod::Center;

        if (state() == SelectMode::SeekSecond) {
            if (isCenter) {
                setValueIfUnset(0, m_internal->radius);
            }
            else {
                setValueIfUnset(0, m_internal->secondPoint.x);
                setValueIfUnset(1, m_internal->secondPoint.y);
            }
        }
        else if (state() == SelectMode::SeekThird) {
            setValueIfUnset(2, m_internal->thirdPoint.x);
            setValueIfUnset(3, m_internal->thirdPoint.y);
        }
    }

    void DrawSketchHandlerCircle::enforceControlParameters(Base::Vector2d& onSketchPos)
    {
        const int expectedBoxes = constructionMethod() == ConstructionMethod::Center ? 1 : 4;
        if (onViewParameters.size() < expectedBoxes) {
            return;  // the tool is not active: there are no boxes to read
        }
        const bool isCenter = constructionMethod() == ConstructionMethod::Center;

        switch (state()) {
        case SelectMode::SeekSecond: {
            if (isCenter) {
                // The centre is known: the radius decides how far out the cursor is taken,
                // and the direction the mouse points at is kept.
                OnViewParameter* radiusBox = onViewParameters.at(0);
                if (radiusBox->isSet()) {
                    Base::Vector2d dir = onSketchPos - m_internal->centerPoint;
                    if (dir.Length() < Precision::Confusion()) {
                        dir.x = 1.0;
                    }
                    const double radius = radiusBox->value();
                    if (radius < Precision::Confusion()) {
                        radiusBox->release();  // a radius of nothing is not a radius
                        return;
                    }
                    onSketchPos = m_internal->centerPoint + radius * dir.Normalize();
                }
            }
            else {
                // The second of the three rim points.
                if (OnViewParameter* x = onViewParameters.at(0); x != nullptr && x->isSet()) {
                    onSketchPos.x = x->value();
                }
                if (OnViewParameter* y = onViewParameters.at(1); y != nullptr && y->isSet()) {
                    onSketchPos.y = y->value();
                }
            }
        } break;
        case SelectMode::SeekThird: {
            // The last of the three rim points.
            if (OnViewParameter* x = onViewParameters.at(2); x != nullptr && x->isSet()) {
                onSketchPos.x = x->value();
            }
            if (OnViewParameter* y = onViewParameters.at(3); y != nullptr && y->isSet()) {
                onSketchPos.y = y->value();
            }
        } break;
        default:
            break;
        }
    }

    void DrawSketchHandlerCircle::updateOnViewParameterPositions()
    {
        adaptParameters(onSketchPos);

        if (m_sceneView == nullptr) {
            return;
        }

        Base::Vector2d anchor = m_internal->secondPoint;
        if (state() == SelectMode::SeekSecond) {
            anchor = constructionMethod() == ConstructionMethod::Center
                ? m_internal->centerPoint
                : m_internal->secondPoint;
        }
        else if (state() == SelectMode::SeekThird) {
            anchor = m_internal->secondPoint;
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
	void DrawSketchHandlerCircle::updateDataAndDrawToPosition(Base::Vector2d onSketchPos)
	{
        switch (state()) {
        case SelectMode::SeekFirst: {
            drawPositionAtCursor(onSketchPos);
            if (constructionMethod() == ConstructionMethod::Center) {
                m_internal->centerPoint = { onSketchPos.x,onSketchPos.y };
                //seekAndRenderAutoConstraint(sugConstraints[0], onSketchPos, Base::Vector2d());
            }
            else {
                m_internal->firstPoint = { onSketchPos.x,onSketchPos.y };
                //seekAndRenderAutoConstraint(
                //    sugConstraints[0],
                //    onSketchPos,
                //    Base::Vector2d(),
                //    AutoConstraint::CURVE
                //);
            }
        } break;
        case SelectMode::SeekSecond: {
            //drawPositionAtCursor(onSketchPos);
            clearPositionAtCursor();
            if (constructionMethod() == ConstructionMethod::ThreeRim) {
                m_internal->centerPoint = (onSketchPos- m_internal->firstPoint) / 2 + m_internal->firstPoint;
            }
            m_internal->secondPoint = { onSketchPos.x,onSketchPos.y };
            m_internal->radius = (onSketchPos- m_internal->centerPoint).Length();
            CreateAndDrawShapeGeometry();
            if (constructionMethod() == ConstructionMethod::Center) {
               // toolWidgetManager.drawDoubleAtCursor(onSketchPos, radius);
            }
            else {
                //toolWidgetManager.drawPositionAtCursor(onSketchPos);
            }

            //seekAndRenderAutoConstraint(
            //    sugConstraints[1],
            //    onSketchPos,
            //    constructionMethod() == ConstructionMethod::Center ? onSketchPos - centerPoint
            //    : Base::Vector2d(),
            //    AutoConstraint::CURVE
            //);
        } break;
        case SelectMode::SeekThird: {
            try {
                if (areCollinear(m_internal->firstPoint, m_internal->secondPoint, onSketchPos)) {
                    // If points are collinear then we can't calculate the center.
                    return;
                }

              
                 auto p=    Part::Geom2dCircle::getCircleCenter( m_internal->firstPoint,  m_internal->secondPoint, onSketchPos);
				 m_internal->centerPoint = { p.x, p.y };
                 m_internal->radius = (onSketchPos -m_internal->centerPoint).Length();
                 m_internal->thirdPoint = onSketchPos;

                drawPositionAtCursor(onSketchPos);

                CreateAndDrawShapeGeometry();

                //seekAndRenderAutoConstraint(
                //    sugConstraints[2],
                //    onSketchPos,
                //    Base::Vector2d(0.f, 0.f),
                //    AutoConstraint::CURVE
                //);
            }
            catch (Base::ValueError& e) {
                e.reportException();
            }
        } break;
        default:
            break;
        }
	}

    void DrawSketchHandlerCircle::onButtonPressed(Base::Vector2d onSketchPos)
    {
        this->updateDataAndDrawToPosition(onSketchPos);
        if (canGoToNextMode()) {
            if (state() == SelectMode::SeekSecond
                && constructionMethod() == ConstructionMethod::Center) {
                setState(SelectMode::End);
            }
            else
            {
                moveToNextMode();
            }
        }
    }

    bool DrawSketchHandlerCircle::canGoToNextMode()
    {
        if (state() == SelectMode::SeekSecond && m_internal->radius < Precision::Confusion()) {
            // Prevent validation of null circle.
            return false;
        }
        return true;
    }

    void DrawSketchHandlerCircle::createShape(bool onlyeditoutline)
    {
        ShapeGeometry.clear();
        if (m_internal->radius < Precision::Confusion()) {
            return;
        }
        addCircleToShapeGeometry(toVector3d(m_internal->centerPoint), m_internal->radius, false);
    }

}
