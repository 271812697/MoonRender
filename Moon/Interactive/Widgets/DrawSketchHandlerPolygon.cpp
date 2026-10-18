#include "Interactive/Widgets/DrawSketchHandlerPolygon.h"
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

#include <numbers>

namespace MOON {

	static Base::Vector3d toVector3d(const Base::Vector2d& vector2d)
	{
		return Base::Vector3d(vector2d.x, vector2d.y, 0.);
	}

	DrawSketchHandlerPolygon::DrawSketchHandlerPolygon(const std::string& name)
		: SupperClass(name)
		, numberOfCorners(6)
		, radius(0.0)
	{
	}

	DrawSketchHandlerPolygon::~DrawSketchHandlerPolygon()
	{
	}

	void DrawSketchHandlerPolygon::onUpdate()
	{
		DrawSketchHandler::onUpdate();
		if (state() == SelectMode::SeekSecond) {
			drawFloatValue(static_cast<float>(radius));
		}
	}

	void DrawSketchHandlerPolygon::onKeyPress(const std::string& key)
	{
		SupperClass::onKeyPress(key);
		if (key == "A") {
			numberOfCorners++;
		}
		else if (key == "S" && numberOfCorners > 3) {
			numberOfCorners--;
		}
		if (state() == SelectMode::SeekSecond) {
			CreateAndDrawShapeGeometry();
		}
	}

	void DrawSketchHandlerPolygon::updateDataAndDrawToPosition(Base::Vector2d onSketchPos)
	{
		switch (state()) {
		case SelectMode::SeekFirst: {
			drawPositionAtCursor(onSketchPos);
			centerPoint = onSketchPos;
		} break;
		case SelectMode::SeekSecond: {
			firstCorner = onSketchPos;
			CreateAndDrawShapeGeometry();
		} break;
		default:
			break;
		}
	}

	bool DrawSketchHandlerPolygon::canGoToNextMode()
	{
		if (state() == SelectMode::SeekSecond && radius < Precision::Confusion()) {
			// Prevent validation of null shape.
			return false;
		}
		return true;
	}

	void DrawSketchHandlerPolygon::createShape(bool onlyeditoutline)
	{
		ShapeGeometry.clear();

		Base::Vector2d prevCorner = firstCorner;
		Base::Vector2d dV = firstCorner - centerPoint;
		radius = dV.Length();
		if (radius < Precision::Confusion()) {
			return;
		}

		const double angleOfSeparation = 2.0 * std::numbers::pi
			/ static_cast<double>(numberOfCorners);
		const double cos_v = std::cos(angleOfSeparation);
		const double sin_v = std::sin(angleOfSeparation);

		double rx = dV.x;
		double ry = dV.y;
		for (unsigned int i = 1; i <= numberOfCorners; i++) {
			const double old_rx = rx;
			rx = cos_v * rx - sin_v * ry;
			ry = cos_v * ry + sin_v * old_rx;
			const Base::Vector2d newCorner(centerPoint.x + rx, centerPoint.y + ry);
			addLineToShapeGeometry(toVector3d(prevCorner), toVector3d(newCorner), false);
			prevCorner = newCorner;
		}

		if (!onlyeditoutline) {
			// The circumscribed construction circle keeps every vertex on a
			// common circle; together with the equal side lengths it preserves
			// the regular polygon property when the sketch is edited later.
			addCircleToShapeGeometry(toVector3d(centerPoint), radius, true);
		}
	}

	void DrawSketchHandlerPolygon::executeCommands()
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
		addPolygonAutoConstraints(firstCurve);
	}

	void DrawSketchHandlerPolygon::addPolygonAutoConstraints(int firstCurve)
	{
		SketcherObj* Obj = SketcherObjManager::instance().GetCurrentActiveSketcherObj();
		if (!Obj) {
			return;
		}

		const int corners = static_cast<int>(numberOfCorners);
		if (corners < 3) {
			return;
		}
		const int circleId = firstCurve + corners;

		// Close the vertex chain: the end of every edge is the start of the
		// next one, and the last edge ends at the first edge's start.
		for (int i = 0; i + 1 < corners; i++) {
			Obj->addConstraint(
				Sketcher::ConstraintType::Coincident,
				firstCurve + i,
				Sketcher::PointPos::end,
				firstCurve + i + 1,
				Sketcher::PointPos::start
			);
		}
		Obj->addConstraint(
			Sketcher::ConstraintType::Coincident,
			firstCurve + corners - 1,
			Sketcher::PointPos::end,
			firstCurve,
			Sketcher::PointPos::start
		);

		// Equal side lengths make the polygon regular once every vertex is
		// also kept on the construction circle below.
		for (int i = 1; i < corners; i++) {
			Obj->addConstraint(
				Sketcher::ConstraintType::Equal,
				firstCurve,
				Sketcher::PointPos::none,
				firstCurve + i,
				Sketcher::PointPos::none
			);
		}

		// Every vertex (each edge's end point) stays on the circumscribed
		// construction circle.
		for (int i = 0; i < corners; i++) {
			Obj->addConstraint(
				Sketcher::ConstraintType::PointOnObject,
				firstCurve + i,
				Sketcher::PointPos::end,
				circleId,
				Sketcher::PointPos::none
			);
		}

		Obj->solve();
		Obj->setConstruction(circleId, true);
	}

}
