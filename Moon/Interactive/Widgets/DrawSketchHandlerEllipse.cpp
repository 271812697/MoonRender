#include "Interactive/Widgets/DrawSketchHandlerEllipse.h"
#include "Interactive/Im3DRenderer.h"
#include "renderer/SceneView.h"
#include "Qtimgui/imgui/imgui.h"
#include "Interactive/Interactive/Event.h"
#include "Interactive/Interactive/ExecuteCommand.h"
#include "Interactive/Interactive/WidgetCallbackMapper.h"
#include "Interactive/Interactive/WidgetEvent.h"
#include "Interactive/Interactive/WidgetEventTranslator.h"
#include "Interactive/Interactive/RenderWindowInteractor.h"
#include "Geometry2d.h"

#include <cmath>

namespace MOON {

	static Base::Vector3d toVector3d(const Base::Vector2d& vector2d)
	{
		return Base::Vector3d(vector2d.x, vector2d.y, 0.);
	}

	class DrawSketchHandlerEllipse::DrawSketchHandlerEllipseInternal {
	public:
		DrawSketchHandlerEllipseInternal(DrawSketchHandlerEllipse* s) : self(s) {
			firstRadius = 0.0;
			secondRadius = 0.0;
			majorRadius = 0.0;
			minorRadius = 0.0;
		}
		~DrawSketchHandlerEllipseInternal() {
		}
	private:
		friend DrawSketchHandlerEllipse;
		DrawSketchHandlerEllipse* self = nullptr;

		Base::Vector2d centerPoint;
		Base::Vector2d periapsis;   // Center: axis endpoint; ThreeRim: first rim point
		Base::Vector2d apoapsis;    // ThreeRim: second rim point
		Base::Vector2d firstAxis, secondAxis;
		double firstRadius, secondRadius, majorRadius, minorRadius;
	};

	DrawSketchHandlerEllipse::DrawSketchHandlerEllipse(const std::string& name)
		: DrawSketchDefaultHandler<DrawSketchHandlerEllipse, StateMachines::ThreeSeekEnd, 3, CircleEllipseConstructionMethod>(name)
		, m_internal(new DrawSketchHandlerEllipseInternal(this))
	{
	}

	DrawSketchHandlerEllipse::~DrawSketchHandlerEllipse()
	{
		delete m_internal;
	}

	void DrawSketchHandlerEllipse::onUpdate()
	{
		DrawSketchHandler::onUpdate();
		if (state() == SelectMode::SeekSecond) {
			drawFloatValue(static_cast<float>(m_internal->firstRadius));
		}
		else if (state() == SelectMode::SeekThird) {
			drawFloatValue(static_cast<float>(m_internal->secondRadius));
		}
	}

	void DrawSketchHandlerEllipse::onSetActive(bool flag)
	{
	}

	void DrawSketchHandlerEllipse::updateDataAndDrawToPosition(Base::Vector2d onSketchPos)
	{
		switch (state()) {
		case SelectMode::SeekFirst: {
			drawPositionAtCursor(onSketchPos);
			if (constructionMethod() == ConstructionMethod::Center) {
				m_internal->centerPoint = onSketchPos;
			}
			else {
				m_internal->apoapsis = onSketchPos;
			}
		} break;
		case SelectMode::SeekSecond: {
			m_internal->periapsis = onSketchPos;

			calculateMajorAxisParameters();

			CreateAndDrawShapeGeometry();

			if (constructionMethod() == ConstructionMethod::Center) {
				drawFloatValue(static_cast<float>(m_internal->firstRadius));
			}
			else {
				drawPositionAtCursor(onSketchPos);
			}
		} break;
		case SelectMode::SeekThird: {
			calculateThroughPointMinorAxisParameters(onSketchPos);

			CreateAndDrawShapeGeometry();

			if (constructionMethod() == ConstructionMethod::Center) {
				drawFloatValue(static_cast<float>(m_internal->secondRadius));
			}
			else {
				drawPositionAtCursor(onSketchPos);
			}
		} break;
		default:
			break;
		}
	}

	void DrawSketchHandlerEllipse::calculateMajorAxisParameters()
	{
		if (constructionMethod() == ConstructionMethod::ThreeRim) {
			// First two rim points define the major axis: center is their midpoint.
			m_internal->centerPoint = (m_internal->apoapsis - m_internal->periapsis) / 2
				+ m_internal->periapsis;
		}
		m_internal->firstAxis = m_internal->periapsis - m_internal->centerPoint;
		m_internal->firstRadius = m_internal->firstAxis.Length();
	}

	void DrawSketchHandlerEllipse::calculateThroughPointMinorAxisParameters(const Base::Vector2d& onSketchPos)
	{
		// de la Hire construction: the cursor defines a point on the ellipse.
		// Project it onto the major axis to recover the parametric angle t:
		//   Px = a*cos(t), Py = b*sin(t)  =>  b = Py / sin(t)
		Base::Vector2d projx;
		projx.ProjectToLine(onSketchPos - m_internal->centerPoint, m_internal->firstAxis);
		Base::Vector2d projy = onSketchPos - m_internal->centerPoint - projx;

		const double lprojx = projx.Length();
		const double lprojy = projy.Length();

		if (lprojx > m_internal->firstRadius) {
			m_internal->secondRadius = lprojy;
		}
		else {
			const double t = std::acos(lprojx / m_internal->firstRadius);
			if (t == 0.0) {
				m_internal->secondRadius = 0.0;
			}
			else {
				m_internal->secondRadius = lprojy / std::sin(t);
			}
		}

		m_internal->secondAxis = projy.Normalize() * m_internal->secondRadius;
	}

	bool DrawSketchHandlerEllipse::canGoToNextMode()
	{
		if (state() == SelectMode::SeekSecond && m_internal->firstRadius < Precision::Confusion()) {
			// Prevent validation of null ellipse.
			return false;
		}
		if (state() == SelectMode::SeekThird
			&& (m_internal->firstRadius < Precision::Confusion()
				|| m_internal->secondRadius < Precision::Confusion())) {
			return false;
		}
		return true;
	}

	void DrawSketchHandlerEllipse::executeCommands()
	{
		SketcherObj* sketch = SketcherObjManager::instance().GetCurrentActiveSketcherObj();
		if (!sketch) {
			return;
		}
		// The ellipse goes in right after the sketch's current content, and the final
		// build (not the preview one) is the geometry that is committed.
		const int firstCurve = sketch->getHighestCurveIndex() + 1;
		createShape(false);
		if (ShapeGeometry.empty()) {
			return;
		}
		SupperClass::executeCommands();

		// FreeCAD exposes the internal geometry of a curve as soon as the tool that
		// drew it commits (its DrawSketchHandlerEllipse calls
		// SketchObject::exposeInternalGeometry). That is what adds the major axis,
		// the minor axis and the two focuses to the sketch: they can be constrained
		// against - a length on the major axis, a point on the minor one, anything on
		// the focuses - they follow the ellipse, and as construction geometry they are
		// drawn dashed and never become part of the sketch's wire.
		//
		// Equal radii degenerate into a circle, which has no internals to expose.
		//
		// The check looks at what the sketch holds, not at ShapeGeometry: the base
		// class hands those entries over to the sketch and leaves them empty, so they
		// cannot be dereferenced after the call above.
		const Part::Geometry* committed = sketch->getGeometry(firstCurve);
		if (committed != nullptr && committed->is<Part::GeomEllipse>()) {
			if (sketch->exposeInternalGeometry(firstCurve) > 0) {
				// The new elements and their alignment constraints are part of the
				// sketch now, so the solver and the drawing have to see them.
				sketch->solve();
			}
		}
	}

	void DrawSketchHandlerEllipse::createShape(bool onlyeditoutline)
	{
		Q_UNUSED(onlyeditoutline);
		ShapeGeometry.clear();

		Base::Vector2d majorAxis = m_internal->firstAxis;
		double majorRadius = m_internal->firstRadius;
		if (state() == SelectMode::SeekSecond) {
			// Preview: fixed 2:1 aspect ratio until the minor axis is picked.
			m_internal->minorRadius = majorRadius * 0.5;
		}
		else {  // SeekThird or End
			m_internal->minorRadius = m_internal->secondRadius;
			if (m_internal->secondRadius > m_internal->firstRadius) {
				majorAxis = m_internal->secondAxis;
				majorRadius = m_internal->secondRadius;
				m_internal->minorRadius = m_internal->firstRadius;
			}
		}
		if (majorRadius < Precision::Confusion() || m_internal->minorRadius < Precision::Confusion()) {
			return;
		}

		if (fabs(m_internal->firstRadius - m_internal->secondRadius) < Precision::Confusion()) {
			// Degenerate case: equal radii collapse to a circle.
			addCircleToShapeGeometry(toVector3d(m_internal->centerPoint), m_internal->firstRadius, false);
		}
		else {
			addEllipseToShapeGeometry(
				toVector3d(m_internal->centerPoint),
				toVector3d(majorAxis),
				majorRadius,
				m_internal->minorRadius,
				false
			);
		}
	}

	void DrawSketchHandlerEllipse::onReset()
	{
		m_internal->firstRadius = 0.0;
		m_internal->secondRadius = 0.0;
		m_internal->majorRadius = 0.0;
		m_internal->minorRadius = 0.0;
	}

}
