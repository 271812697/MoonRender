#include "Interactive/Widgets/DrawSketchHandlerSlot.h"
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

#include <algorithm>
#include <cmath>
#include <numbers>

namespace MOON {

	static Base::Vector3d toVector3d(const Base::Vector2d& vector2d)
	{
		return Base::Vector3d(vector2d.x, vector2d.y, 0.);
	}

	DrawSketchHandlerSlot::DrawSketchHandlerSlot(const std::string& name)
		: SupperClass(name)
		, radius(1.0)
		, length(0.0)
		, angle(0.0)
	{
	}

	DrawSketchHandlerSlot::~DrawSketchHandlerSlot()
	{
	}

	void DrawSketchHandlerSlot::onUpdate()
	{
		DrawSketchHandler::onUpdate();
		if (state() == SelectMode::SeekSecond) {
			drawFloatValue(static_cast<float>(length));
		}
		else if (state() == SelectMode::SeekThird) {
			drawFloatValue(static_cast<float>(radius));
		}
	}

	void DrawSketchHandlerSlot::updateDataAndDrawToPosition(Base::Vector2d onSketchPos)
	{
		switch (state()) {
		case SelectMode::SeekFirst: {
			drawPositionAtCursor(onSketchPos);
			startPoint = onSketchPos;
		} break;
		case SelectMode::SeekSecond: {
			secondPoint = onSketchPos;
			angle = (secondPoint - startPoint).Angle();
			length = (secondPoint - startPoint).Length();
			const double scale = 0.2;
			radius = length * scale;  // radius chosen at 1/5 of length
			CreateAndDrawShapeGeometry();
		} break;
		case SelectMode::SeekThird: {
			// Radius adapts to the cursor: if the cursor is "between" the two
			// centers, use its distance to the center line, otherwise use the
			// distance to the nearer center.
			const double L1 = (onSketchPos - startPoint).Length();
			const double L2 = (onSketchPos - secondPoint).Length();

			if ((L1 * L1 + length * length > L2 * L2)
				&& (L2 * L2 + length * length > L1 * L1)) {
				// distance of cursor to the line startPoint-secondPoint
				radius = std::abs(
					(secondPoint.y - startPoint.y) * onSketchPos.x
					- (secondPoint.x - startPoint.x) * onSketchPos.y
					+ secondPoint.x * startPoint.y - secondPoint.y * startPoint.x)
					/ length;
			}
			else {
				radius = std::min(L1, L2);
			}

			CreateAndDrawShapeGeometry();
		} break;
		default:
			break;
		}
	}

	bool DrawSketchHandlerSlot::canGoToNextMode()
	{
		if (state() == SelectMode::SeekSecond && length < Precision::Confusion()) {
			// Prevent validation of null slot.
			return false;
		}
		if (state() == SelectMode::SeekThird
			&& (length < Precision::Confusion() || radius < Precision::Confusion())) {
			return false;
		}
		return true;
	}

	void DrawSketchHandlerSlot::createShape(bool onlyeditoutline)
	{
		Q_UNUSED(onlyeditoutline);
		using std::numbers::pi;

		ShapeGeometry.clear();
		if (length < Precision::Confusion() || radius < Precision::Confusion()) {
			return;
		}

		Part::GeomArcOfCircle* arc1 = addArcToShapeGeometry(
			toVector3d(startPoint),
			pi / 2 + angle,
			1.5 * pi + angle,
			radius,
			false
		);
		Part::GeomArcOfCircle* arc2 = addArcToShapeGeometry(
			toVector3d(secondPoint),
			1.5 * pi + angle,
			pi / 2 + angle,
			radius,
			false
		);

		Base::Vector3d p11 = arc1->getStartPoint();
		Base::Vector3d p12 = arc1->getEndPoint();
		Base::Vector3d p21 = arc2->getStartPoint();
		Base::Vector3d p22 = arc2->getEndPoint();

		addLineToShapeGeometry(p11, p22, false);
		addLineToShapeGeometry(p12, p21, false);
	}

	void DrawSketchHandlerSlot::executeCommands()
	{
		SketcherObj* Obj = SketcherObjManager::instance().GetCurrentActiveSketcherObj();
		if (!Obj) {
			return;
		}

		const int firstCurve = Obj->getHighestCurveIndex() + 1;
		createShape(false);
		if (ShapeGeometry.empty()) {
			return;
		}

		SupperClass::executeCommands();
		addSlotAutoConstraints(firstCurve);
	}

	void DrawSketchHandlerSlot::addSlotAutoConstraints(int firstCurve)
	{
		SketcherObj* Obj = SketcherObjManager::instance().GetCurrentActiveSketcherObj();
		if (!Obj) {
			return;
		}

		// Geometry layout: arc0 = left cap, arc1 = right cap, line0 = top
		// connector, line1 = bottom connector. Each connector is tangent to the
		// cap it touches (a point-wise tangent also pins the shared vertex).
		Obj->addConstraint(
			Sketcher::ConstraintType::Tangent,
			firstCurve, Sketcher::PointPos::start,
			firstCurve + 2, Sketcher::PointPos::start
		);
		Obj->addConstraint(
			Sketcher::ConstraintType::Tangent,
			firstCurve, Sketcher::PointPos::end,
			firstCurve + 3, Sketcher::PointPos::start
		);
		Obj->addConstraint(
			Sketcher::ConstraintType::Tangent,
			firstCurve + 1, Sketcher::PointPos::end,
			firstCurve + 2, Sketcher::PointPos::end
		);
		Obj->addConstraint(
			Sketcher::ConstraintType::Tangent,
			firstCurve + 1, Sketcher::PointPos::start,
			firstCurve + 3, Sketcher::PointPos::end
		);

		// Both caps keep the same radius so the slot width stays uniform.
		Obj->addConstraint(
			Sketcher::ConstraintType::Equal,
			firstCurve, Sketcher::PointPos::none,
			firstCurve + 1, Sketcher::PointPos::none
		);

		Obj->solve();
	}

	void DrawSketchHandlerSlot::onReset()
	{
		length = 0.0;
		angle = 0.0;
	}

}
