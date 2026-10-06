#include "Interactive/Widgets/SmartDimensionWidget.h"

#include "Interactive/Im3DRenderer.h"
#include "Interactive/Interactive/Event.h"
#include "Interactive/Interactive/ExecuteCommand.h"
#include "Interactive/Interactive/WidgetCallbackMapper.h"
#include "Interactive/Interactive/WidgetEvent.h"
#include "Sketcher/SketcherObjManager.h"
#include "Sketcher/SketcherObj.h"
#include "Sketcher/SketcherObjWidget.h"
#include "Geometry.h"
#include "core/Global/ServiceLocator.h"
#include "core/log.h"
#include "editor/Toolbar/sketchToolbar.h"
#include "renderer/SceneView.h"
#include "Qtimgui/imgui/imgui.h"
#include "Qtimgui/implot/implotCustom.h"

#include <QDialog>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include <Precision.hxx>

namespace MOON
{
	namespace
	{
		// The markup uses the same primitives as the sketch's own dimension captions,
		// so a preview and the constraint it turns into look alike.
		const ImU32 kPreviewColor = IM_COL32(243, 71, 255, 255);
		constexpr float kShaftThickness = 2.0f;
		constexpr float kExtensionThickness = 1.0f;
		constexpr double kPi = 3.14159265358979323846;

		/** Asks for a dimension value, pre-filled with what the geometry measures
		 * now so that pressing OK leaves the sketch as it is.
		 *
		 * \return false when the user cancelled. */
		bool askForValue(const char* p_title, const char* p_label, double p_value, double& p_out)
		{
			QDialog dialog;
			dialog.setWindowTitle(p_title);
			dialog.setModal(true);

			auto* layout = new QVBoxLayout(&dialog);
			layout->setContentsMargins(20, 20, 20, 20);
			layout->setSpacing(12);
			auto* form = new QFormLayout();
			form->setLabelAlignment(Qt::AlignRight);
			layout->addLayout(form);

			auto* spin = new QDoubleSpinBox(&dialog);
			spin->setRange(-1.0e6, 1.0e6);
			spin->setDecimals(3);
			spin->setSingleStep(1.0);
			spin->setValue(p_value);
			form->addRow(new QLabel(p_label, &dialog), spin);

			auto* ok = new QPushButton(QStringLiteral("ok"), &dialog);
			auto* cancel = new QPushButton(QStringLiteral("cancel"), &dialog);
			auto* buttons = new QHBoxLayout();
			buttons->addStretch();
			buttons->addWidget(ok);
			buttons->addWidget(cancel);
			layout->addLayout(buttons);
			QObject::connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);
			QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);

			if (dialog.exec() != QDialog::Accepted) {
				return false;
			}
			p_out = spin->value();
			return true;
		}
	}

	class SmartDimensionWidget::Internal
	{
	public:
		explicit Internal(SmartDimensionWidget* p_self) : self(p_self)
		{
		}
		~Internal()
		{
		}

		/** What a pick is, in the terms a dimension cares about. */
		enum class Kind
		{
			None,
			Line,     ///< a line segment
			Point,    ///< an end point or a centre
			Circle,   ///< a full circle
			Arc,      ///< an arc of a circle
			Ellipse,  ///< an ellipse or an arc of one
			Other
		};

		/** One picked element with what is needed to measure it. */
		struct Pick
		{
			int geoId = -1;
			SketcherObj::PointPos pos = SketcherObj::PointPos::none;
			Kind kind = Kind::None;
			/** The ends of a line, or the place of a point. */
			Base::Vector2d first;
			Base::Vector2d second;
			/** Centre and radius of a circle or an arc. */
			Base::Vector2d centre;
			double radius = 0.0;

			bool isPoint() const { return kind == Kind::Point; }
			bool isLine() const { return kind == Kind::Line; }
			bool isCircular() const { return kind == Kind::Circle || kind == Kind::Arc; }
		};

		SketcherObj* activeSketch() const
		{
			SketcherObj* sketch
			= SketcherObjManager::instance().GetCurrentActiveSketcherObj();
			SketcherObjWidget* widget
			= SketcherObjManager::instance().GetCurrentActiveSketcherWidget();
			if (sketch == nullptr || widget == nullptr || !widget->InEdit()) {
				return nullptr;
			}
			return sketch;
		}

		void reset()
		{
			picks.clear();
			type = Sketcher::ConstraintType::None;
			secondCandidate = false;
			dragged = false;
			placingClick = false;
		}

		/** True while the picks are a pair of circles or arcs, which are measured
		 * between their centres. */
		bool isAngle() const
		{
			return type == Sketcher::ConstraintType::Angle;
		}

		/** The angle the two picked lines make and the ends it is measured between,
		 * measured the way the angle tool measures it (the sketcher's rule, which
		 * SketcherObj shares with the constraint toolbar): from the end of each line
		 * that points away from the corner. Those ends go on the constraint with the
		 * value, so the solver holds the angle the annotation shows instead of having
		 * to turn one of the lines to satisfy its supplement. */
		bool measuredAngle(
			int& p_firstGeoId,
			SketcherObj::PointPos& p_firstPos,
			int& p_secondGeoId,
			SketcherObj::PointPos& p_secondPos,
			double& p_radians) const
		{
			if (picks.size() != 2 || !picks[0].isLine() || !picks[1].isLine()) {
				return false;
			}
			const SketcherObj::MeasuredAngle measured = SketcherObj::measureAngleBetweenLineEnds(
				picks[0].first,
				picks[0].second,
				picks[1].first,
				picks[1].second
			);
			if (!measured.usable) {
				return false;  // parallel lines: there is no angle between them
			}
			p_firstGeoId = measured.swapped ? picks[1].geoId : picks[0].geoId;
			p_firstPos = measured.firstPos;
			p_secondGeoId = measured.swapped ? picks[0].geoId : picks[1].geoId;
			p_secondPos = measured.secondPos;
			p_radians = measured.radians;
			return true;
		}

		/** The angle the picks make, in radians (0 when they make none). */
		double angleRadians() const
		{
			int firstGeoId = -1;
			int secondGeoId = -1;
			SketcherObj::PointPos firstPos = SketcherObj::PointPos::none;
			SketcherObj::PointPos secondPos = SketcherObj::PointPos::none;
			double radians = 0.0;
			if (!measuredAngle(firstGeoId, firstPos, secondGeoId, secondPos, radians)) {
				return 0.0;
			}
			return radians;
		}

		/** The two points a distance would be measured between, as the picks define
		 * them: the ends of a line, the picked point itself, the centre of a circle,
		 * and - for a point against a line - the foot of its perpendicular.
		 *
		 * \return false when the picks do not describe a distance at all. */
		bool distanceAnchors(Base::Vector2d& a, Base::Vector2d& b) const
		{
			if (picks.size() == 1) {
				const Pick& pick = picks[0];
				if (pick.isPoint()) {
					a = pick.first;
					b = Base::Vector2d(0.0, 0.0);
					return true;
				}
				if (pick.isLine()) {
					a = pick.first;
					b = pick.second;
					return true;
				}
				return false;
			}
			if (picks.size() != 2) {
				return false;
			}

			const Pick& first = picks[0];
			const Pick& second = picks[1];
			if (first.isPoint() && second.isPoint()) {
				a = first.first;
				b = second.first;
				return true;
			}
			if (first.isCircular() && second.isCircular()) {
				a = first.centre;
				b = second.centre;
				return true;
			}
			if (first.isPoint() && second.isLine()) {
				a = first.first;
				return footOnLine(a, second, b);
			}
			if (first.isLine() && second.isPoint()) {
				b = second.first;
				return footOnLine(b, first, a);
			}
			return false;
		}

		/** The perpendicular distance of p_point from a line, and the foot of that
		 * perpendicular as p_foot. */
		static bool footOnLine(
			const Base::Vector2d& p_point,
			const Pick& p_line,
			Base::Vector2d& p_foot)
		{
			Base::Vector2d direction = p_line.second - p_line.first;
			const double length = direction.Length();
			if (length < Precision::Confusion()) {
				return false;
			}
			direction = direction / length;
			const double along = (p_point - p_line.first) * direction;
			p_foot = p_line.first + direction * along;
			return true;
		}

		/** The normal of a direction, in the sketch plane. */
		static Base::Vector2d normalOf(const Base::Vector2d& p_direction)
		{
			return Base::Vector2d(-p_direction.y, p_direction.x);
		}

		/** Where two lines meet; false when they run parallel to each other. */
		static bool lineIntersection(
			const Pick& p_first,
			const Pick& p_second,
			Base::Vector2d& p_corner)
		{
			const Base::Vector2d d1 = p_first.second - p_first.first;
			const Base::Vector2d d2 = p_second.second - p_second.first;
			const double determinant = d1.x * d2.y - d1.y * d2.x;
			if (std::abs(determinant) < 1.0e-9) {
				return false;
			}
			const Base::Vector2d delta = p_second.first - p_first.first;
			const double t = (delta.x * d2.y - delta.y * d2.x) / determinant;
			p_corner = p_first.first + d1 * t;
			return true;
		}

		/** What the dimension measures with the geometry as it stands now; an angle
		 * is reported in degrees, which is what the dialog and the caption show. */
		double measure() const
		{
			switch (type) {
				case Sketcher::ConstraintType::Radius:
					return picks.empty() ? 0.0 : picks[0].radius;
				case Sketcher::ConstraintType::Diameter:
					return picks.empty() ? 0.0 : picks[0].radius * 2.0;
				case Sketcher::ConstraintType::Angle:
					return angleRadians() * 180.0 / kPi;
				case Sketcher::ConstraintType::DistanceX:
				case Sketcher::ConstraintType::DistanceY:
				case Sketcher::ConstraintType::Distance: {
					Base::Vector2d a;
					Base::Vector2d b;
					if (!distanceAnchors(a, b)) {
						return 0.0;
					}
					if (type == Sketcher::ConstraintType::DistanceX) {
						return std::fabs(a.x - b.x);
					}
					if (type == Sketcher::ConstraintType::DistanceY) {
						return std::fabs(a.y - b.y);
					}
					return (a - b).Length();
				}
				default:
					return 0.0;
			}
		}

		SmartDimensionWidget* self = nullptr;

		std::vector<Pick> picks;
		Sketcher::ConstraintType type = Sketcher::ConstraintType::None;
		bool secondCandidate = false;

		/** Where the left button went down, in screen pixels, and whether the mouse
		 * travelled far enough since for the release to count as a placement. */
		Base::Vector2d pressScreen;
		bool dragged = false;
		bool placingClick = false;

		/** Where the cursor is, in sketch space. */
		Base::Vector2d cursor;
		/** Where the dimension was dropped, in screen pixels: the annotation is put
		 * there when it is added, so that it does not jump to its default place. */
		Base::Vector2d placeScreen;
	};

	SmartDimensionWidget::SmartDimensionWidget(const std::string& name)
		: DrawSketchHandler(name)
		, mInternal(new Internal(this))
	{
		setActive(false);
	}

	SmartDimensionWidget::~SmartDimensionWidget()
	{
		delete mInternal;
	}

	void SmartDimensionWidget::onSetActive(bool flag)
	{
		// A tool that is switched on starts without picks: the last ones belong to
		// the run before.
		mInternal->reset();
	}

	void SmartDimensionWidget::onUpdate()
	{
		// The tool only has something to do while a sketch is open, and it shares the
		// mouse with the drawing handlers. When either of those stops being true it
		// turns itself off, so that its button never stays pressed on its own.
		if (mInternal->activeSketch() == nullptr) {
			leaveTool();
			return;
		}
		for (const auto& widget : renderer->getGizmoWidgets()) {
			if (widget.second == nullptr || widget.second == this
				|| !widget.second->isActived()) {
				continue;
			}
			if (dynamic_cast<DrawSketchHandler*>(widget.second) != nullptr) {
				leaveTool();
				return;
			}
		}

		DrawSketchHandler::onUpdate();
		drawPreview();
	}

	Base::Vector2d SmartDimensionWidget::cursorOnSketchPlane() const
	{
		// The drawing handlers snap the cursor to points and curves; a dimension is
		// placed freely, so the plain hit on the sketch plane is used here.
		SketcherObj* sketch = mInternal->activeSketch();
		if (sketch == nullptr) {
			return Base::Vector2d(0.0, 0.0);
		}
		const SketcherPlane2D plane2d = sketch->getPlane();
		const auto ray = m_sceneView->GetMouseRay();
		Maths::FVector3 hit;
		ray.hitPlane(
			Maths::FVector3(plane2d.normal.x, plane2d.normal.y, plane2d.normal.z),
			plane2d.normal.Dot(plane2d.origin),
			hit);
		const Base::Vector3d hitPos(hit.x, hit.y, hit.z);
		return Base::Vector2d(
			(hitPos - plane2d.origin).Dot(plane2d.xAxis),
			(hitPos - plane2d.origin).Dot(plane2d.yAxis));
	}

	Eigen::Vector2f SmartDimensionWidget::screenOfSketchPos(const Base::Vector2d& p_pos) const
	{
		// The same mapping the annotations use: going through the sketch plane and the
		// camera keeps the two in one coordinate system, which the raw mouse position
		// of the window is not guaranteed to be in.
		SketcherObj* sketch = mInternal->activeSketch();
		if (sketch == nullptr) {
			return Eigen::Vector2f(0.0f, 0.0f);
		}
		const SketcherPlane2D plane2d = sketch->getPlane();
		const Base::Vector3d world = plane2d.origin + p_pos.x * plane2d.xAxis
			+ p_pos.y * plane2d.yAxis;
		return renderer->worldToScreen(Eigen::Vector3f(
			static_cast<float>(world.x),
			static_cast<float>(world.y),
			static_cast<float>(world.z)));
	}

	void SmartDimensionWidget::onMouseMove()
	{
		if (mInternal->activeSketch() == nullptr) {
			return;
		}
		mInternal->cursor = cursorOnSketchPlane();

		// Moving the mouse while the button is down drags the dimension into place:
		// that release then counts as the placement.
		if (!mInternal->picks.empty() && !mInternal->placingClick) {
			auto [mx, my] = m_sceneView->getInutState().GetMousePosition();
			const Base::Vector2d moved(mx, my);
			if ((moved - mInternal->pressScreen).Length() > 3.0) {
				mInternal->dragged = true;
			}
		}

		// The base class keeps onSketchPos and the snap marker in step with the ray.
		DrawSketchHandler::onMouseMove();
		updateCandidate();
	}

	void SmartDimensionWidget::onLeftMousePressed()
	{
		auto [mx, my] = m_sceneView->getInutState().GetMousePosition();
		mInternal->pressScreen = Base::Vector2d(mx, my);
		mInternal->dragged = false;
		mInternal->placingClick = false;

		mInternal->cursor = cursorOnSketchPlane();

		// Two elements are as much as any dimension of this tool takes, so once they
		// are picked a click only says where the dimension goes.
		if (mInternal->picks.size() < 2 && pickAtCursor()) {
			updateCandidate();
			return;
		}

		// The click landed next to the geometry: it places the dimension, and the
		// release asks for the value.
		if (!mInternal->picks.empty()) {
			mInternal->placingClick = true;
		}
		updateCandidate();
	}

	void SmartDimensionWidget::onLeftMouseReleased()
	{
		if (mInternal->picks.empty() || mInternal->type == Sketcher::ConstraintType::None) {
			return;
		}
		// A drag that ends away from where it started places the dimension; a click
		// next to the geometry does the same. A click that only picked waits for the
		// next one, so that the preview can be moved before it is put down.
		if (mInternal->placingClick || mInternal->dragged) {
			applyCandidate();
		}
	}

	void SmartDimensionWidget::onRightMousePressed()
	{
		// The habits of the drawing tools: a right click ends what is being done,
		// and only leaves the tool when there is nothing left to end.
		if (!mInternal->picks.empty()) {
			mInternal->reset();
			return;
		}
		leaveTool();
	}

	void SmartDimensionWidget::leaveTool()
	{
		// DrawSketchHandler::quit() also unchecks the button of the sketch toolbar,
		// which knows nothing about this tool (its name is not among the drawing
		// handlers). The button of the constraint toolbar follows the widget through
		// its poll instead, so switching the widget off is enough here.
		setActive(false);
	}

	void SmartDimensionWidget::onKeyPress(const std::string& key)
	{
		if (key == "M") {
			// The other one of the two dimensions a circle or an arc can take.
			if (mInternal->picks.size() == 1 && mInternal->picks[0].isCircular()) {
				mInternal->secondCandidate = !mInternal->secondCandidate;
				updateCandidate();
			}
			return;
		}
		if (key == "ESC" || key == "ESCAPE") {
			if (!mInternal->picks.empty()) {
				mInternal->reset();
			}
			else {
				leaveTool();
			}
		}
	}

	bool SmartDimensionWidget::pickAtCursor()
	{
		SketcherObj* sketch = mInternal->activeSketch();
		if (sketch == nullptr) {
			return false;
		}

		SketcherObjWidget* widget = SketcherObjManager::instance().GetCurrentActiveSketcherWidget();
		const SketcherObj::SelectGeoId picked = widget->testSelect(mInternal->cursor);
		if (picked.GeoId == SketcherObj::NoGeoId) {
			return false;
		}

		// Clicking an element that is already picked drops it again, which is how a
		// wrong pick is taken back without losing the other one.
		for (size_t i = 0; i < mInternal->picks.size(); ++i) {
			if (mInternal->picks[i].geoId == picked.GeoId
				&& mInternal->picks[i].pos == picked.pointPos) {
				mInternal->picks.erase(mInternal->picks.begin() + static_cast<std::ptrdiff_t>(i));
				return true;
			}
		}

		const Part::Geometry* geometry = sketch->resolveGeometry(picked.GeoId);
		if (geometry == nullptr) {
			return false;
		}

		Internal::Pick pick;
		pick.geoId = picked.GeoId;
		pick.pos = picked.pointPos;
		if (picked.pointPos != SketcherObj::PointPos::none) {
			// A point of a curve: measured either against the origin or against the
			// second pick.
			pick.kind = Internal::Kind::Point;
			if (!sketch->getGeometryPoint(picked.GeoId, picked.pointPos, pick.first)) {
				return false;
			}
		}
		else if (geometry->is<Part::GeomCircle>()) {
			pick.kind = Internal::Kind::Circle;
			pick.radius = static_cast<const Part::GeomCircle*>(geometry)->getRadius();
		}
		else if (geometry->is<Part::GeomArcOfCircle>()) {
			pick.kind = Internal::Kind::Arc;
			pick.radius = static_cast<const Part::GeomArcOfCircle*>(geometry)->getRadius();
		}
		else if (geometry->is<Part::GeomEllipse>()
			|| geometry->is<Part::GeomArcOfEllipse>()) {
			// An ellipse has no dimension the solver can take without its internal
			// geometry - the centre and the two axes FreeCAD adds when the ellipse is
			// drawn - which this sketch does not create yet.
			CORE_WARN(
				"[Dimension] an ellipse is measured by its major and minor diameter, "
				"which needs the internal geometry of the ellipse; that is not in the "
				"sketch yet");
			return false;
		}
		else if (geometry->is<Part::GeomLineSegment>()) {
			pick.kind = Internal::Kind::Line;
			const auto* line = static_cast<const Part::GeomLineSegment*>(geometry);
			pick.first = Base::Vector2d(line->getStartPoint().x, line->getStartPoint().y);
			pick.second = Base::Vector2d(line->getEndPoint().x, line->getEndPoint().y);
		}
		else {
			pick.kind = Internal::Kind::Other;
			CORE_INFO("[Dimension] geometry {} has no smart dimension", picked.GeoId);
			return false;
		}

		if (pick.isCircular()) {
			// The mid point of a round curve is its centre (see getCurveSegment), which
			// is where the annotation starts its radius from as well.
			if (!sketch->getGeometryPoint(picked.GeoId, SketcherObj::PointPos::mid, pick.centre)) {
				return false;
			}
		}

		mInternal->picks.push_back(pick);
		return true;
	}

	void SmartDimensionWidget::updateCandidate()
	{
		Internal& state = *mInternal;
		if (state.picks.empty()) {
			state.type = Sketcher::ConstraintType::None;
			return;
		}

		// A single circle or arc is measured by its radius; M switches it to the
		// diameter and back, because that is the other dimension the same element can
		// take. (A circle used to start on the diameter, which is not what was picked:
		// the element the cursor is on is round in either case, and the radius is what
		// a sketch usually drives a round profile by.)
		if (state.picks.size() == 1 && state.picks[0].isCircular()) {
			state.type = state.secondCandidate ? Sketcher::ConstraintType::Diameter
											   : Sketcher::ConstraintType::Radius;
			return;
		}

		// Two lines are measured by the angle between them - unless they run parallel
		// to each other, where there is no angle to put a dimension on.
		if (state.picks.size() == 2 && state.picks[0].isLine() && state.picks[1].isLine()) {
			int firstGeoId = -1;
			int secondGeoId = -1;
			SketcherObj::PointPos firstPos = SketcherObj::PointPos::none;
			SketcherObj::PointPos secondPos = SketcherObj::PointPos::none;
			double radians = 0.0;
			state.type = state.measuredAngle(
							 firstGeoId, firstPos, secondGeoId, secondPos, radians)
				? Sketcher::ConstraintType::Angle
				: Sketcher::ConstraintType::None;
			return;
		}

		// Everything else is a distance, and which one is FreeCAD's rule: the two
		// points span a box, and where the cursor sits relative to that box says
		// whether the horizontal, the vertical or the aligned distance is meant.
		// Only meaning something for the distance kinds; the angle and the radius
		// keep their own elements below.
		Base::Vector2d a(0.0, 0.0);
		Base::Vector2d b(0.0, 0.0);
		if (!state.distanceAnchors(a, b)) {
			state.type = Sketcher::ConstraintType::None;
			return;
		}
		const Base::Vector2d& cursor = state.cursor;
		const double minX = std::min(a.x, b.x);
		const double maxX = std::max(a.x, b.x);
		const double minY = std::min(a.y, b.y);
		const double maxY = std::max(a.y, b.y);
		const bool onHorizontal = std::abs(a.y - b.y) < Precision::Confusion();
		const bool onVertical = std::abs(a.x - b.x) < Precision::Confusion();
		const bool besideX = cursor.x > minX && cursor.x < maxX
			&& (cursor.y < minY || cursor.y > maxY);
		const bool besideY = cursor.y > minY && cursor.y < maxY
			&& (cursor.x < minX || cursor.x > maxX);
		const bool diagonal
			= ((cursor.y < minY || cursor.y > maxY) && (cursor.x < minX || cursor.x > maxX))
			|| (cursor.x > minX && cursor.x < maxX && cursor.y > minY && cursor.y < maxY);
		// A single point can only be measured along an axis, and a point against a
		// line is measured perpendicular to it, so those two keep their one dimension.
		const bool singlePoint
			= state.picks.size() == 1 && state.picks[0].isPoint();
		const bool pointToLine = state.picks.size() == 2
			&& ((state.picks[0].isPoint() && state.picks[1].isLine())
				|| (state.picks[0].isLine() && state.picks[1].isPoint()));

		if (singlePoint) {
			// A point is measured along whichever axis the cursor leans towards, so
			// that both the horizontal and the vertical distance are reachable.
			const Base::Vector2d toPoint(
				cursor.x - state.picks[0].first.x, cursor.y - state.picks[0].first.y);
			state.type = std::abs(toPoint.x) >= std::abs(toPoint.y)
				? Sketcher::ConstraintType::DistanceX
				: Sketcher::ConstraintType::DistanceY;
		}
		else if (pointToLine) {
			state.type = Sketcher::ConstraintType::Distance;
		}
		else if (onHorizontal || besideX) {
			state.type = Sketcher::ConstraintType::DistanceX;
		}
		else if (onVertical || besideY) {
			state.type = Sketcher::ConstraintType::DistanceY;
		}
		else if (diagonal || !onHorizontal) {
			state.type = Sketcher::ConstraintType::Distance;
		}
	}

	void SmartDimensionWidget::drawPreview()
	{
		Internal& state = *mInternal;
		if (state.picks.empty() || state.type == Sketcher::ConstraintType::None) {
			return;
		}
		ImDrawList* drawList = ImGui::GetForegroundDrawList();
		if (drawList == nullptr) {
			return;
		}

		const auto toScreen = [this](const Base::Vector2d& sketchPos) {
			return renderer->worldToScreen(plane.valueEigen(sketchPos));
		};
		const auto toImVec = [](const Eigen::Vector2f& p) { return ImVec2(p.x(), p.y()); };
		const auto angleOf = [](const ImVec2& from, const ImVec2& to) {
			// The screen y axis points down, so the angle is taken the other way round;
			// the sketch's own labels do the same.
			return -std::atan2(to.y - from.y, to.x - from.x) * 180.0f
				/ static_cast<float>(kPi);
		};

		// The same wording the sketch puts on its own dimension captions.
		char buffer[96] = {0};
		switch (state.type) {
			case Sketcher::ConstraintType::Radius:
				std::snprintf(buffer, sizeof(buffer), "R%.2f", state.measure());
				break;
			case Sketcher::ConstraintType::Diameter:
				std::snprintf(buffer, sizeof(buffer), "D%.2f", state.measure());
				break;
			default:
				std::snprintf(buffer, sizeof(buffer), "%.2f", state.measure());
				break;
		}
		const std::string text(buffer);

		if (state.picks.size() == 1 && state.picks[0].isCircular()) {
			// A radial line towards the cursor; a diameter runs through the centre to
			// the far side.
			const Internal::Pick& pick = state.picks[0];
			Base::Vector2d direction = state.cursor - pick.centre;
			if (direction.Length() < Precision::Confusion()) {
				direction = Base::Vector2d(1.0, 0.0);
			}
			direction.Normalize();
			const bool diameter = state.type == Sketcher::ConstraintType::Diameter;
			const Base::Vector2d firstEnd
				= diameter ? pick.centre - direction * pick.radius : pick.centre;
			const Base::Vector2d secondEnd = pick.centre + direction * pick.radius;
			const ImVec2 first = toImVec(toScreen(firstEnd));
			const ImVec2 second = toImVec(toScreen(secondEnd));
			drawList->AddLine(first, second, kPreviewColor, kShaftThickness);
			const float length = std::sqrt(
				(second.x - first.x) * (second.x - first.x)
				+ (second.y - first.y) * (second.y - first.y));
			if (length <= 1.0f) {
				return;
			}
			if (diameter) {
				ImPlotCustom::drawDoubleArrow(
					ImPlotCustom::Transform(first, angleOf(first, second)),
					kPreviewColor,
					length,
					kShaftThickness,
					text.c_str());
			}
			else {
				// A radius carries one arrow, at the circle, and its value beside the
				// line. AddArrow puts its tip a head length past the end of the line,
				// so the line stops that much short and the tip lands on the circle.
				const float head = 2.0f * 2.5f * kShaftThickness;
				ImPlotCustom::AddArrow(
					ImPlotCustom::Transform(first, angleOf(first, second)),
					kPreviewColor,
					std::max(length - head, 1.0f),
					kShaftThickness);
				// The caption sits just outside the rim, which is where the sketch puts
				// it once the constraint is there.
				const float dirX = (second.x - first.x);
				const float dirY = (second.y - first.y);
				const float len = std::sqrt(dirX * dirX + dirY * dirY);
				const float captionDist = length + 18.0f;
				const ImVec2 caption(
					len > 1.0f ? first.x + dirX / len * captionDist : first.x,
					len > 1.0f ? first.y + dirY / len * captionDist : first.y);
				ImPlotCustom::AddTextTransform(
					ImPlotCustom::Transform(caption, 0.0f), kPreviewColor, text.c_str());
			}
			return;
		}

		if (state.isAngle()) {
			// An arc at the corner the two lines share, spanning the angle the
			// constraint will hold: from the direction the first stored end reads in
			// to the second one, through the size the caption shows.
			int firstGeoId = -1;
			int secondGeoId = -1;
			SketcherObj::PointPos firstPos = SketcherObj::PointPos::none;
			SketcherObj::PointPos secondPos = SketcherObj::PointPos::none;
			double measuredRadians = 0.0;
			if (!state.measuredAngle(
					firstGeoId, firstPos, secondGeoId, secondPos, measuredRadians)) {
				return;
			}
			const Internal::Pick& firstLine = state.picks[0];
			const Internal::Pick& secondLine = state.picks[1];
			Base::Vector2d corner = firstLine.first;
			if (!Internal::lineIntersection(firstLine, secondLine, corner)) {
				corner = firstLine.first;
			}
			const ImVec2 cornerScreen = toImVec(toScreen(corner));
			// The two lines the picks hold, in the order the constraint names them.
			const Internal::Pick& measuredFirst = firstGeoId == firstLine.geoId ? firstLine : secondLine;
			const Internal::Pick& measuredSecond = secondGeoId == firstLine.geoId ? firstLine : secondLine;
			Base::Vector2d d1 = measuredFirst.second - measuredFirst.first;
			Base::Vector2d d2 = measuredSecond.second - measuredSecond.first;
			if (d1.Length() < Precision::Confusion() || d2.Length() < Precision::Confusion()) {
				return;
			}
			d1.Normalize();
			d2.Normalize();
			// `start` reads a line from its start to its end, `end` the other way
			// round; the ends are what the constraint stores.
			if (firstPos == SketcherObj::PointPos::end) {
				d1 = -d1;
			}
			if (secondPos == SketcherObj::PointPos::end) {
				d2 = -d2;
			}
			const ImVec2 screen1 = toImVec(toScreen(corner + d1));
			const float start = angleOf(cornerScreen, screen1);
			// The screen's y axis points down, so the counter-clockwise angle the
			// constraint holds is drawn the other way round.
			const float sweep
				= -static_cast<float>(measuredRadians) * 180.0f / static_cast<float>(kPi);
			// The arc sits at the distance of the cursor from the corner, which is what
			// pulling the annotation in and out does once it is there.
			const Eigen::Vector2f cursorScreen = screenOfSketchPos(state.cursor);
			const float cursorDx = cursorScreen.x() - cornerScreen.x;
			const float cursorDy = cursorScreen.y() - cornerScreen.y;
			const float radius = std::max(
				std::sqrt(cursorDx * cursorDx + cursorDy * cursorDy), 8.0f);
			ImPlotCustom::drawDoubleArcArrow(
				ImPlotCustom::Transform(cornerScreen, start),
				kPreviewColor,
				radius,
				sweep,
				kShaftThickness,
				text.c_str());
			return;
		}

		Base::Vector2d a;
		Base::Vector2d b;
		if (!state.distanceAnchors(a, b)) {
			return;
		}

		// The dimension line follows the cursor, and extension lines tie its ends back
		// to the points it measures.
		Base::Vector2d from;
		Base::Vector2d to;
		if (state.type == Sketcher::ConstraintType::DistanceX) {
			from = Base::Vector2d(a.x, state.cursor.y);
			to = Base::Vector2d(b.x, state.cursor.y);
		}
		else if (state.type == Sketcher::ConstraintType::DistanceY) {
			from = Base::Vector2d(state.cursor.x, a.y);
			to = Base::Vector2d(state.cursor.x, b.y);
		}
		else {
			// Aligned: the line keeps the direction of the two points and only its
			// distance from them follows the cursor.
			Base::Vector2d direction = b - a;
			if (direction.Length() < Precision::Confusion()) {
				direction = Base::Vector2d(1.0, 0.0);
			}
			direction.Normalize();
			const Base::Vector2d normal = Internal::normalOf(direction);
			const double offset = (state.cursor - a) * normal;
			from = a + normal * offset;
			to = b + normal * offset;
		}

		const ImVec2 fromScreen = toImVec(toScreen(from));
		const ImVec2 toScreen2 = toImVec(toScreen(to));
		drawList->AddLine(
			toImVec(toScreen(a)), fromScreen, kPreviewColor, kExtensionThickness);
		drawList->AddLine(
			toImVec(toScreen(b)), toScreen2, kPreviewColor, kExtensionThickness);

		const float length = std::sqrt(
			(toScreen2.x - fromScreen.x) * (toScreen2.x - fromScreen.x)
			+ (toScreen2.y - fromScreen.y) * (toScreen2.y - fromScreen.y));
		if (length < 4.0f) {
			return;
		}
		ImPlotCustom::drawDoubleArrow(
			ImPlotCustom::Transform(fromScreen, angleOf(fromScreen, toScreen2)),
			kPreviewColor,
			length,
			kShaftThickness,
			text.c_str());
	}

	void SmartDimensionWidget::applyCandidate()
	{
		SketcherObj* sketch = mInternal->activeSketch();
		if (sketch == nullptr) {
			return;
		}

		// The anchors of the distance kinds; they stay at the origin for the angle and
		// the radius, which build their own elements below.
		Base::Vector2d a(0.0, 0.0);
		Base::Vector2d b(0.0, 0.0);
		mInternal->distanceAnchors(a, b);
		// What the geometry measures now, with the sign convention the constraint
		// toolbar uses for its own distance tools: the solver stores the difference
		// as second minus first, so the datum has to follow it or the two points
		// would swap places.
		const double axisCurrent = mInternal->type == Sketcher::ConstraintType::DistanceY
			? a.y - b.y
			: a.x - b.x;
		const double axisSign = axisCurrent < 0.0 ? -1.0 : 1.0;
		const double diffSign = axisCurrent < 0.0 ? 1.0 : -1.0;

		const char* title = "Distance";
		switch (mInternal->type) {
			case Sketcher::ConstraintType::DistanceX:
				title = "DistanceX";
				break;
			case Sketcher::ConstraintType::DistanceY:
				title = "DistanceY";
				break;
			case Sketcher::ConstraintType::Radius:
				title = "Radius";
				break;
			case Sketcher::ConstraintType::Diameter:
				title = "Diameter";
				break;
			case Sketcher::ConstraintType::Angle:
				title = "Angle";
				break;
			default:
				break;
		}
		const double value = mInternal->measure();
		{
			// The annotation is added where the preview stands, not where the sketch
			// would put it by default. The cursor is taken through the sketch plane,
			// where it is already known, rather than from the window, so that the place
			// is measured in the same screen space the annotation is drawn in.
			const Eigen::Vector2f screen = screenOfSketchPos(mInternal->cursor);
			mInternal->placeScreen
				= Base::Vector2d(static_cast<double>(screen.x()), static_cast<double>(screen.y()));
		}

		// The dialog is opened from the event loop rather than from inside the mouse
		// event that caused it: a modal dialog nested in the input handling would let
		// the release reach the viewport a second time.
		const Internal captured = *mInternal;
		QTimer::singleShot(0, [this, sketch, captured, value, title, axisSign, diffSign]() {
			SketcherObj* active = mInternal->activeSketch();
			if (active == nullptr || active != sketch) {
				return;
			}
			double entered = value;
			if (!askForValue(title, title, value, entered)) {
				return;
			}

			auto constraint = std::make_unique<Sketcher::Constraint>();
			constraint->Type = captured.type;
			const std::vector<Internal::Pick>& picks = captured.picks;
			if (captured.type == Sketcher::ConstraintType::Angle) {
				// The solver keeps an angle in radians; the dialog asked in degrees. The
				// ends the angle is measured between go on the constraint with the value:
				// they are what makes it the angle the annotation showed instead of its
				// supplement.
				int firstGeoId = -1;
				int secondGeoId = -1;
				SketcherObj::PointPos firstPos = SketcherObj::PointPos::none;
				SketcherObj::PointPos secondPos = SketcherObj::PointPos::none;
				double measured = 0.0;
				if (!captured.measuredAngle(
						firstGeoId, firstPos, secondGeoId, secondPos, measured)) {
					return;
				}
				constraint->First = firstGeoId;
				constraint->FirstPos = firstPos;
				constraint->Second = secondGeoId;
				constraint->SecondPos = secondPos;
				constraint->setValue(entered * kPi / 180.0);
			}
			else if (captured.type == Sketcher::ConstraintType::Radius
				|| captured.type == Sketcher::ConstraintType::Diameter) {
				constraint->First = picks[0].geoId;
				constraint->setValue(entered);
			}
			else if (picks.size() == 1) {
				constraint->First = picks[0].geoId;
				if (picks[0].isPoint()) {
					// A point against the origin carries only its own position.
					constraint->FirstPos = picks[0].pos;
					constraint->setValue(axisSign * entered);
				}
				else {
					constraint->setValue(diffSign * entered);
				}
			}
			else if (picks.size() == 2) {
				if (picks[0].isPoint() && picks[1].isPoint()) {
					constraint->First = picks[0].geoId;
					constraint->FirstPos = picks[0].pos;
					constraint->Second = picks[1].geoId;
					constraint->SecondPos = picks[1].pos;
					constraint->setValue(diffSign * entered);
				}
				else if (picks[0].isPoint() && picks[1].isLine()) {
					constraint->First = picks[0].geoId;
					constraint->FirstPos = picks[0].pos;
					constraint->Second = picks[1].geoId;
					constraint->setValue(entered);
				}
				else if (picks[0].isLine() && picks[1].isPoint()) {
					constraint->First = picks[1].geoId;
					constraint->FirstPos = picks[1].pos;
					constraint->Second = picks[0].geoId;
					constraint->setValue(entered);
				}
				else if (picks[0].isCircular() && picks[1].isCircular()) {
					constraint->First = picks[0].geoId;
					constraint->Second = picks[1].geoId;
					constraint->setValue(entered);
				}
				else {
					return;
				}
			}
			else {
				return;
			}

			// An identical constraint is updated instead of stacked: the tool is also
			// the way to change a dimension that is already there.
			const int existing = active->findConstraint(constraint.get());
			int added = existing;
			if (existing >= 0) {
				const int error = active->setDatum(existing, constraint->getValue());
				if (error != 0) {
					CORE_WARN("[Dimension] updating the datum failed, solver error {}", error);
				}
			}
			else {
				added = active->addConstraint(std::move(constraint));
				const int error = active->solve();
				if (error != 0) {
					// FreeCAD pulls a datum back when the sketch cannot hold it
					// (SketchObject::setDatum); a constraint that is being added has
					// no earlier value to fall back on, so it goes again instead -
					// kept, it would leave every later solve failing, dragging too.
					if (added >= 0) {
						active->removeConstraint(added);
						added = -1;
					}
					CORE_WARN(
						"[Dimension] adding the constraint failed, solver error {}; it "
						"was dropped and the sketch is left as it was",
						error);
					mInternal->reset();
					return;
				}
			}
			// Put the annotation where the preview was, or the sketch would draw it at
			// its default place instead.
			if (SketcherObjWidget* activeWidget = SketcherObjManager::instance().GetCurrentActiveSketcherWidget()) {
				activeWidget->placeDimensionAnnotation(
					added,
					static_cast<float>(captured.placeScreen.x),
					static_cast<float>(captured.placeScreen.y)
				);
			}
			mInternal->reset();
		});
	}
}



