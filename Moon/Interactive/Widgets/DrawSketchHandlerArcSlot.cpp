#include "Interactive/Widgets/DrawSketchHandlerArcSlot.h"
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

#include <cmath>
#include <numbers>

namespace MOON {

	static Base::Vector3d toVector3d(const Base::Vector2d& vector2d)
	{
		return Base::Vector3d(vector2d.x, vector2d.y, 0.);
	}

	DrawSketchHandlerArcSlot::DrawSketchHandlerArcSlot(const std::string& name)
		: SupperClass(name)
		, startAngle(0.0)
		, startAngleBackup(0.0)
		, endAngle(0.0)
		, arcAngle(0.0)
		, r(0.0)
		, radius(0.0)
		, angleReversed(false)
	{
	}

	DrawSketchHandlerArcSlot::~DrawSketchHandlerArcSlot()
	{
	}

	void DrawSketchHandlerArcSlot::onUpdate()
	{
		DrawSketchHandler::onUpdate();
		if (state() == SelectMode::SeekThird) {
			drawFloatValue(static_cast<float>(arcAngle * 180.0 / std::numbers::pi));
		}
		else if (state() == SelectMode::SeekFourth) {
			drawFloatValue(static_cast<float>(r));
		}
	}

	void DrawSketchHandlerArcSlot::updateDataAndDrawToPosition(Base::Vector2d onSketchPos)
	{
		switch (state()) {
		case SelectMode::SeekFirst: {
			drawPositionAtCursor(onSketchPos);
			centerPoint = onSketchPos;
		} break;
		case SelectMode::SeekSecond: {
			startPoint = onSketchPos;
			startAngle = (startPoint - centerPoint).Angle();
			startAngleBackup = startAngle;
			radius = (startPoint - centerPoint).Length();

			CreateAndDrawShapeGeometry();
		} break;
		case SelectMode::SeekThird: {
			endPoint = centerPoint + (onSketchPos - centerPoint).Normalize() * radius;
			if (constructionMethod() == ConstructionMethod::ArcSlot) {
				const double scale = 10;
				r = radius / scale;
			}
			else {
				const double scale = 1.2;
				r = radius * scale;
			}

			startAngle = startAngleBackup;

			const double angle1 = (onSketchPos - centerPoint).Angle() - startAngle;
			const double angle2 = angle1 + (angle1 < 0. ? 2 : -2) * std::numbers::pi;
			arcAngle = std::abs(angle1 - arcAngle) < std::abs(angle2 - arcAngle) ? angle1 : angle2;

			reverseIfNecessary();

			CreateAndDrawShapeGeometry();
		} break;
		case SelectMode::SeekFourth: {
			if (constructionMethod() == ConstructionMethod::ArcSlot) {
				r = std::min(radius, std::abs(radius - (onSketchPos - centerPoint).Length()));
			}
			else {
				r = (onSketchPos - centerPoint).Length();
			}

			CreateAndDrawShapeGeometry();
		} break;
		default:
			break;
		}
	}

	bool DrawSketchHandlerArcSlot::canGoToNextMode()
	{
		// Prevent validation of null arc.
		if (state() == SelectMode::SeekSecond && radius < Precision::Confusion()) {
			return false;
		}
		if (state() == SelectMode::SeekThird && std::fabs(arcAngle) < Precision::Confusion()) {
			return false;
		}
		if (state() == SelectMode::SeekFourth) {
			if (constructionMethod() == ConstructionMethod::ArcSlot) {
				if (r < Precision::Confusion()) {
					return false;
				}
			}
			else {
				if (std::fabs(radius - r) < Precision::Confusion()) {
					return false;
				}
			}
		}
		return true;
	}

	void DrawSketchHandlerArcSlot::reverseIfNecessary()
	{
		if (arcAngle > 0) {
			endAngle = startAngle + arcAngle;
			angleReversed = false;
		}
		else {
			endAngle = startAngle;
			startAngle = startAngle + arcAngle;
			angleReversed = true;
		}
	}

	void DrawSketchHandlerArcSlot::createShape(bool onlyeditoutline)
	{
		Q_UNUSED(onlyeditoutline);
		using std::numbers::pi;

		ShapeGeometry.clear();
		if (radius < Precision::Confusion()) {
			return;
		}

		if (state() == SelectMode::SeekSecond) {
			// Preview: full circle until the arc angle is picked.
			addCircleToShapeGeometry(toVector3d(centerPoint), radius, false);
		}
		else {
			if (std::fabs(arcAngle) < Precision::Confusion()) {
				return;
			}
			if (state() == SelectMode::SeekFourth && r < Precision::Confusion()) {
				return;
			}

			if (constructionMethod() == ConstructionMethod::ArcSlot) {
				// Outer arc, two round caps, and (when wide enough) the inner arc.
				addArcToShapeGeometry(
					toVector3d(centerPoint),
					startAngle,
					endAngle,
					radius + r,
					false
				);
				addArcToShapeGeometry(
					toVector3d(startPoint),
					angleReversed ? endAngle : startAngle + pi,
					angleReversed ? endAngle + pi : startAngle + 2 * pi,
					r,
					false
				);
				addArcToShapeGeometry(
					toVector3d(endPoint),
					angleReversed ? startAngle + pi : endAngle,
					angleReversed ? startAngle + 2 * pi : pi + endAngle,
					r,
					false
				);
				if (radius - r > Precision::Confusion()) {
					addArcToShapeGeometry(
						toVector3d(centerPoint),
						startAngle,
						endAngle,
						radius - r,
						false
					);
				}
			}
			else {  // RectangleSlot: outer arc + inner arc + two radial walls
				Part::GeomArcOfCircle* arc1 = addArcToShapeGeometry(
					toVector3d(centerPoint),
					startAngle,
					endAngle,
					radius,
					false
				);
				Base::Vector3d p11 = arc1->getStartPoint();
				Base::Vector3d p12 = arc1->getEndPoint();

				if (r > Precision::Confusion()) {
					auto arc2 = std::make_unique<Part::GeomArcOfCircle>();
					arc2->setRadius(r);
					arc2->setRange(startAngle, endAngle, true);
					arc2->setCenter(toVector3d(centerPoint));

					Base::Vector3d p21 = arc2->getStartPoint();
					Base::Vector3d p22 = arc2->getEndPoint();

					addLineToShapeGeometry(p11, p21, false);
					addLineToShapeGeometry(p12, p22, false);

					// Inner arc is pushed last to keep the element order stable.
					ShapeGeometry.push_back(std::move(arc2));
				}
				else {
					addLineToShapeGeometry(p11, toVector3d(centerPoint), false);
					addLineToShapeGeometry(p12, toVector3d(centerPoint), false);
				}
			}
		}
	}

	void DrawSketchHandlerArcSlot::executeCommands()
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
		if (constructionMethod() == ConstructionMethod::ArcSlot) {
			addArcSlotAutoConstraints(firstCurve);
		}
		else {
			addRectangleSlotAutoConstraints(firstCurve);
		}
	}

	void DrawSketchHandlerArcSlot::addArcSlotAutoConstraints(int firstCurve)
	{
		SketcherObj* Obj = SketcherObjManager::instance().GetCurrentActiveSketcherObj();
		if (!Obj) {
			return;
		}

		// Geometry layout: outer arc 0, cap arc 1 at the start point, cap arc 2
		// at the end point and (when wide enough) inner arc 3. The outer and
		// inner arcs share the center, and every joint is tangent continuous.
		const bool allArcs = std::fabs(radius - r) > Precision::Confusion();
		const Sketcher::PointPos pos1 = angleReversed
			? Sketcher::PointPos::start
			: Sketcher::PointPos::end;
		const Sketcher::PointPos pos2 = angleReversed
			? Sketcher::PointPos::end
			: Sketcher::PointPos::start;

		if (allArcs) {
			Obj->addConstraint(
				Sketcher::ConstraintType::Coincident,
				firstCurve, Sketcher::PointPos::mid,
				firstCurve + 3, Sketcher::PointPos::mid
			);
			Obj->addConstraint(
				Sketcher::ConstraintType::Tangent,
				firstCurve + 3, pos1,
				firstCurve + 2, pos1
			);
			Obj->addConstraint(
				Sketcher::ConstraintType::Tangent,
				firstCurve + 3, pos2,
				firstCurve + 1, pos2
			);
		}
		else {
			// The inner arc degenerates; the two caps meet at the center.
			Obj->addConstraint(
				Sketcher::ConstraintType::Coincident,
				firstCurve, Sketcher::PointPos::mid,
				firstCurve + 1, pos2
			);
			Obj->addConstraint(
				Sketcher::ConstraintType::Coincident,
				firstCurve, Sketcher::PointPos::mid,
				firstCurve + 2, pos1
			);
		}

		Obj->addConstraint(
			Sketcher::ConstraintType::Tangent,
			firstCurve, pos1,
			firstCurve + 2, pos2
		);
		Obj->addConstraint(
			Sketcher::ConstraintType::Tangent,
			firstCurve, pos2,
			firstCurve + 1, pos1
		);

		Obj->solve();
	}

	void DrawSketchHandlerArcSlot::addRectangleSlotAutoConstraints(int firstCurve)
	{
		SketcherObj* Obj = SketcherObjManager::instance().GetCurrentActiveSketcherObj();
		if (!Obj) {
			return;
		}

		// Geometry layout: outer arc 0, radial wall 1, radial wall 2 and (when
		// present) inner arc 3. Both walls stay radial to the outer arc and the
		// inner arc is concentric with the outer one.
		const bool allGeos = r > Precision::Confusion();
		Obj->addConstraint(
			Sketcher::ConstraintType::Perpendicular,
			firstCurve, Sketcher::PointPos::none,
			firstCurve + 1, Sketcher::PointPos::none
		);
		Obj->addConstraint(
			Sketcher::ConstraintType::Perpendicular,
			firstCurve, Sketcher::PointPos::none,
			firstCurve + 2, Sketcher::PointPos::none
		);
		Obj->addConstraint(
			Sketcher::ConstraintType::Coincident,
			firstCurve, Sketcher::PointPos::start,
			firstCurve + 1, Sketcher::PointPos::start
		);
		Obj->addConstraint(
			Sketcher::ConstraintType::Coincident,
			firstCurve, Sketcher::PointPos::end,
			firstCurve + 2, Sketcher::PointPos::start
		);

		if (allGeos) {
			Obj->addConstraint(
				Sketcher::ConstraintType::Coincident,
				firstCurve, Sketcher::PointPos::mid,
				firstCurve + 3, Sketcher::PointPos::mid
			);
			Obj->addConstraint(
				Sketcher::ConstraintType::Coincident,
				firstCurve + 3, Sketcher::PointPos::start,
				firstCurve + 1, Sketcher::PointPos::end
			);
			Obj->addConstraint(
				Sketcher::ConstraintType::Coincident,
				firstCurve + 3, Sketcher::PointPos::end,
				firstCurve + 2, Sketcher::PointPos::end
			);
		}
		else {
			// The inner arc degenerates to the center point.
			Obj->addConstraint(
				Sketcher::ConstraintType::Coincident,
				firstCurve, Sketcher::PointPos::mid,
				firstCurve + 1, Sketcher::PointPos::end
			);
			Obj->addConstraint(
				Sketcher::ConstraintType::Coincident,
				firstCurve, Sketcher::PointPos::mid,
				firstCurve + 2, Sketcher::PointPos::end
			);
		}

		Obj->solve();
	}

}
