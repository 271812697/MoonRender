#include "editor/Toolbar/ConstraintGroups.h"
#include "editor/Toolbar/RibbonGroup.h"
#include "editor/Command/command.h"
#include "Core/Global/ServiceLocator.h"
#include "renderer/SceneView.h"
#include "Sketcher/SketcherObjManager.h"
#include "Sketcher/SketcherObj.h"
#include "Interactive/Widgets/DrawSketchHandler.h"
#include "editor/Toolbar/sketchToolbar.h"
#include "renderer/GizmoRenderPass.h"
#include "Geometry.h"
#include "core/log.h"
#include <QCoreApplication>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QTimer>
#include <QToolBar>
#include <cmath>
#include <initializer_list>
namespace MOON {
	/** What the sketch panel works on: the selection lives in the editing widget,
	 * not in the sketch data. */
	static std::vector<SketcherObj::SelectGeoId> currentSketchSelection()
	{
		SketcherObj* obj
		= SketcherObjManager::instance().GetCurrentActiveSketcherObj();
		return obj != nullptr ? obj->getSelectGeoPosIds()
		: std::vector<SketcherObj::SelectGeoId>();
	}
	// ---- helpers: derive constraint dialog defaults from the current sketch
	static double dist2d(const Base::Vector2d& a, const Base::Vector2d& b)
	{
		const double dx = a.x - b.x;
		const double dy = a.y - b.y;
		return std::sqrt(dx * dx + dy * dy);
	}
	static bool lineEnds2d(SketcherObj* obj, int geoId, Base::Vector2d& s, Base::Vector2d& e)
	{
		const Part::Geometry* g = obj->resolveGeometry(geoId);
		if (!g || !g->is<Part::GeomLineSegment>()) {
			return false;
		}
		const auto* line = static_cast<const Part::GeomLineSegment*>(g);
		const Base::Vector3d p1 = line->getStartPoint();
		const Base::Vector3d p2 = line->getEndPoint();
		s = Base::Vector2d(p1.x, p1.y);
		e = Base::Vector2d(p2.x, p2.y);
		return true;
	}
	// Returns the signed coordinate difference currently enforced by the
	// solver for the X/Y distance tools (order follows the selection order).
	static double signedAxisCurrent(
		SketcherObj* obj,
		const std::vector<SketcherObj::SelectGeoId>& sel,
		bool horizontal
	)
	{
		Base::Vector2d a, b;
		if (sel.size() == 2) {
			auto getAny = [&](const SketcherObj::SelectGeoId& item, Base::Vector2d& out) {
				if (item.pointPos != SketcherObj::PointPos::none) {
					return obj->getGeometryPoint(item.GeoId, item.pointPos, out);
				}
				Base::Vector2d unused;
				return lineEnds2d(obj, item.GeoId, out, unused);
			};
			if (getAny(sel[0], a) && getAny(sel[1], b)) {
				return horizontal ? a.x - b.x : a.y - b.y;
			}
		}
		else if (sel.size() == 1) {
			if (sel[0].pointPos == SketcherObj::PointPos::none) {
				if (lineEnds2d(obj, sel[0].GeoId, a, b)) {
					return horizontal ? a.x - b.x : a.y - b.y;
				}
			}
			else if (obj->getGeometryPoint(sel[0].GeoId, sel[0].pointPos, a)) {
				return horizontal ? a.x : a.y;
			}
		}
		return 0.0;
	}
	static double lengthCurrent(SketcherObj* obj, const std::vector<SketcherObj::SelectGeoId>& sel)
	{
		if (sel.size() == 1 && sel[0].pointPos == SketcherObj::PointPos::none) {
			const Part::Geometry* g = obj->resolveGeometry(sel[0].GeoId);
			if (!g) {
				return 0.0;
			}
			if (g->is<Part::GeomLineSegment>()) {
				const auto* line = static_cast<const Part::GeomLineSegment*>(g);
				return dist2d(
					Base::Vector2d(line->getStartPoint().x, line->getStartPoint().y),
					Base::Vector2d(line->getEndPoint().x, line->getEndPoint().y)
				);
			}
			if (g->is<Part::GeomArcOfCircle>()) {
				const auto* arc = static_cast<const Part::GeomArcOfCircle*>(g);
				double u = 0.0, v = 0.0;
				arc->getRange(u, v, true);
				return std::fabs(v - u) * arc->getRadius();
			}
		}
		if (sel.size() == 2) {
			Base::Vector2d p1, p2;
			const bool p1ok = sel[0].pointPos != SketcherObj::PointPos::none
				&& obj->getGeometryPoint(sel[0].GeoId, sel[0].pointPos, p1);
			const bool p2ok = sel[1].pointPos != SketcherObj::PointPos::none
				&& obj->getGeometryPoint(sel[1].GeoId, sel[1].pointPos, p2);
			if (p1ok && p2ok) {
				return dist2d(p1, p2);
			}
			if (p1ok && sel[1].pointPos == SketcherObj::PointPos::none) {
				const Part::Geometry* g2 = obj->resolveGeometry(sel[1].GeoId);
				if (g2 && g2->is<Part::GeomLineSegment>()) {
					Base::Vector2d s, e;
					if (lineEnds2d(obj, sel[1].GeoId, s, e)) {
						Base::Vector2d ab(e.x - s.x, e.y - s.y);
						const double len2 = ab.x * ab.x + ab.y * ab.y;
						if (len2 > 1.0e-12) {
							const double t = ((p1.x - s.x) * ab.x + (p1.y - s.y) * ab.y) / len2;
							const Base::Vector2d foot(
								s.x + t * ab.x,
								s.y + t * ab.y
							);
							return dist2d(p1, foot);
						}
					}
				}
			}
			// circle/arc to circle/arc: distance between centres
			auto centerOf = [&](const SketcherObj::SelectGeoId& item, Base::Vector2d& out) {
				if (item.pointPos == SketcherObj::PointPos::none) {
					return obj->getGeometryPoint(item.GeoId, SketcherObj::PointPos::mid, out);
				}
				return obj->getGeometryPoint(item.GeoId, item.pointPos, out);
			};
			if (centerOf(sel[0], p1) && centerOf(sel[1], p2)) {
				return dist2d(p1, p2);
			}
		}
		return 0.0;
	}
	static double radiusCurrent(SketcherObj* obj, const SketcherObj::SelectGeoId& sel)
	{
		const Part::Geometry* g = obj->resolveGeometry(sel.GeoId);
		if (!g) {
			return 0.0;
		}
		if (g->is<Part::GeomCircle>()) {
			return static_cast<const Part::GeomCircle*>(g)->getRadius();
		}
		if (g->is<Part::GeomArcOfCircle>()) {
			return static_cast<const Part::GeomArcOfCircle*>(g)->getRadius();
		}
		return 0.0;
	}
	static double angleCurrentDeg(SketcherObj* obj, const std::vector<SketcherObj::SelectGeoId>& sel)
	{
		const double pi = 3.14159265358979323846;
		// The angle a single line makes with the sketch's horizontal axis. A pair of
		// lines is measured by SketcherObj::measureAngleBetweenLines() instead: that
		// one knows which ends the constraint is measured between, which a plain
		// direction cannot say.
		if (sel.size() == 1 && sel[0].pointPos == SketcherObj::PointPos::none) {
			Base::Vector2d s, e;
			if (lineEnds2d(obj, sel[0].GeoId, s, e)) {
				double deg = std::atan2(e.y - s.y, e.x - s.x) * 180.0 / pi;
				if (deg < 0.0) {
					deg += 360.0;
				}
				return deg;
			}
		}
		return 90.0;
	}
	class ParamDialog : public QDialog
	{
		
	public:
		struct ParamDef
		{
			std::string name;
			float range[2];
			int decimal = 3;
			float singleStep = 1.0f;
			float value;
			ParamDef(
				const std::string&n,float a,float b,
				float v,int dec=3,float step=1.0):name(n),decimal(dec),singleStep(step),value(v)
			{
				range[0] = a;
				range[1] = b;
			}
		};
		explicit ParamDialog(const std::string&name,QWidget* parent = nullptr) {
			setWindowTitle(name.c_str());
			setModal(true);
		}
		void setUp() {
			// 总布局
			QVBoxLayout* layMain = new QVBoxLayout(this);
			layMain->setContentsMargins(20, 20, 20, 20);
			layMain->setSpacing(12);
			// 表单布局（核心，多行参数）
			QFormLayout* m_formLayout = new QFormLayout();
			m_formLayout->setLabelAlignment(Qt::AlignRight);
			m_formLayout->setSpacing(10);
			layMain->addLayout(m_formLayout);
			for (auto it : paramList) {
				QLabel* lableTip = new QLabel(it.name.c_str());
				QDoubleSpinBox* widget = new QDoubleSpinBox();
				paramWidgetList[it.name] = widget;

				// 数值框参数（匹配CAD软件）
				widget->setRange(it.range[0], it.range[1]); // 最小0.001，避免0半径
				widget->setDecimals(it.decimal);           // 3位小数，CAD精度
				widget->setValue(it.value);
				widget->setSingleStep(it.singleStep);
				m_formLayout->addRow(lableTip, widget);
			}
			QPushButton* btnOk = new QPushButton("ok");
			QPushButton* btnCancel = new QPushButton("cancle");



			QHBoxLayout* layBtn = new QHBoxLayout();
			layBtn->addStretch();
			layBtn->addWidget(btnOk);
			layBtn->addWidget(btnCancel);

			layMain->addLayout(layBtn);
			connect(btnOk, &QPushButton::clicked, this, &QDialog::accept);
			connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
		}
		void addParamDef(const ParamDef& def) {
			paramList.push_back(def);
		}
		float getParamValue(const std::string& name) {
			return paramWidgetList[name]->value();
		}
	private:
		std::vector<ParamDef>paramList;
		std::unordered_map<std::string, QDoubleSpinBox*>paramWidgetList;
		
	};
	// If a constraint with the same type/elements already exists, treat the
	// dialog value as an edit (setDatum) instead of stacking a duplicate.
	void addOrSetDatumConstraint(SketcherObj* Obj, std::unique_ptr<Sketcher::Constraint> constraint)
	{
		const auto logConstraint = [](const char* action, const Sketcher::Constraint* c) {
			CORE_INFO(
				"{} constraint type={} first={}/{} second={}/{} third={}/{} value={}",
				action,
				c ? c->typeToString() : "null",
				c ? c->First : -2000,
				c ? static_cast<int>(c->FirstPos) : 0,
				c ? c->Second : -2000,
				c ? static_cast<int>(c->SecondPos) : 0,
				c ? c->Third : -2000,
				c ? static_cast<int>(c->ThirdPos) : 0,
				c ? c->getValue() : 0.0
			);
		};
		const int existing = Obj->findConstraint(constraint.get());
		if (existing >= 0) {
			logConstraint("update", constraint.get());
			const int err = Obj->setDatum(existing, constraint->getValue());
			if (err != 0) {
				CORE_WARN("update constraint datum failed, solver error {}", err);
			}
			else {
				CORE_INFO("updated existing constraint id {} to {}", existing, constraint->getValue());
			}
		}
		else {
			logConstraint("add", constraint.get());
			const int id = Obj->addConstraint(std::move(constraint));
			CORE_INFO("added constraint id {}", id);
			const int err = Obj->solve();
			if (err != 0) {
				// FreeCAD pulls a datum back when the sketch cannot hold it
				// (SketchObject::setDatum). A constraint that is being added has no
				// earlier value to fall back on, so it goes again instead: kept, it
				// would leave every later solve failing - dragging included, since a
				// drag solves through the same system.
				if (id >= 0) {
					Obj->removeConstraint(id);
				}
				CORE_WARN(
					"add constraint failed, solver error {}; the constraint was dropped "
					"and the sketch is left as it was",
					err);
			}
			else {
				CORE_INFO("solve ok after add constraint id {}", id);
			}
		}
	}
	/** The smart dimension tool: a button that turns a widget on and off.
	 *
	 * Unlike the other buttons of this toolbar it is not a one-shot command - the
	 * tool stays on until it is pressed again - so its action is checkable. The
	 * clicks it works on are the sketch's, which means whatever draw handler was
	 * running has to let go of them first. */
	class SmartDimensionCommand : public Command
	{
	public:
		SmartDimensionCommand(QObject* parent) :Command(parent) {
			auto action = new QAction(this);
			action->setCheckable(true);
			setAction(action);
			// The widget turns itself off when the sketch closes or another drawing
			// tool starts, so the button follows it instead of the other way around.
			auto* poll = new QTimer(this);
			poll->setInterval(250);
			QObject::connect(poll, &QTimer::timeout, this, [this]() {
				// The tool lives on the viewport's renderer, and the viewport is only
				// there once the workspace is: while the start page is up there is
				// nothing to ask, and the tool cannot have been turned on either.
				if (!Core::Global::ServiceLocator::IsProvided<Editor::Panels::SceneView>()) {
					return;
				}
				auto& view = GetService(Editor::Panels::SceneView);
				auto& gizmoPass = view.GetRenderer()
					.GetPass<Editor::Rendering::GizmoRenderPass>("ImRenderer");
				const bool on = gizmoPass.isEnableGizmoWidget("SmartDimension");
				if (this->action()->isChecked() != on) {
					this->action()->setChecked(on);
				}
			});
			poll->start();
		}
	protected:
		virtual void execute()override {
			const bool value = action()->isChecked();
			auto& view = GetService(Editor::Panels::SceneView);
			auto& gizmoPass
				= view.GetRenderer().GetPass<Editor::Rendering::GizmoRenderPass>("ImRenderer");

			if (value) {
				if (SketcherObjManager::instance().GetCurrentActiveSketcherObj() == nullptr) {
					CORE_WARN(
						"[Dimension] the smart dimension works inside a sketch; open one "
						"first");
					action()->setChecked(false);
					return;
				}
				// One tool at a time: leave whatever draw handler was running, and
				// bring its button up with it.
				for (auto& widget : gizmoPass.getGizmoWidgets()) {
					if (widget.second == nullptr || widget.first == "SmartDimension"
						|| !widget.second->isActived()) {
						continue;
					}
					if (dynamic_cast<DrawSketchHandler*>(widget.second) == nullptr) {
						continue;
					}
					gizmoPass.enableGizmoWidget(widget.first, false);
					GetService(SketchToolbar).setUncheckedAction(widget.first);
				}
			}
			gizmoPass.enableGizmoWidget("SmartDimension", value);
		}
	};
	class  ConstraintCommand : public Command
	{
	public:
		ConstraintCommand(QObject* parent) :Command(parent) {
			auto action = new QAction(this);
			action->setCheckable(false);
			setAction(action);
		}
	};
	class CoincidentConstraint :public ConstraintCommand {
	public:
		CoincidentConstraint(QObject* parent):ConstraintCommand(parent) {
		}
	protected:
		virtual void execute()override {
			SketcherObj* Obj = SketcherObjManager::instance().GetCurrentActiveSketcherObj();
			std::vector<SketcherObj::SelectGeoId> listOfGeoIds = currentSketchSelection();

			if (listOfGeoIds.size() > 1) {
				if (listOfGeoIds.size() == 2) {
					Sketcher::ConstraintType constrType = Sketcher::ConstraintType::None;
					if (listOfGeoIds[0].pointPos != SketcherObj::PointPos::none && listOfGeoIds[1].pointPos != SketcherObj::PointPos::none) {
						constrType = Sketcher::ConstraintType::Coincident;
						Obj->addConstraint(
							constrType,
							listOfGeoIds[0].GeoId,
							listOfGeoIds[0].pointPos,
							listOfGeoIds[1].GeoId,
							listOfGeoIds[1].pointPos);
					}
					else if (listOfGeoIds[0].pointPos == SketcherObj::PointPos::none && listOfGeoIds[1].pointPos != SketcherObj::PointPos::none) {
						constrType = Sketcher::ConstraintType::PointOnObject;
						Obj->addConstraint(
							constrType,
							listOfGeoIds[1].GeoId,
							listOfGeoIds[1].pointPos,
							listOfGeoIds[0].GeoId,
							listOfGeoIds[0].pointPos);
					}
					else if (listOfGeoIds[0].pointPos != SketcherObj::PointPos::none && listOfGeoIds[1].pointPos == SketcherObj::PointPos::none) {
						constrType = Sketcher::ConstraintType::PointOnObject;
						Obj->addConstraint(
							constrType,
							listOfGeoIds[0].GeoId,
							listOfGeoIds[0].pointPos,
							listOfGeoIds[1].GeoId,
							listOfGeoIds[1].pointPos);

					}
					else {
						CORE_ERROR("both are edges");
					}
				}
				else {
					Obj->addConstraint(
						Sketcher::ConstraintType::Coincident,
						listOfGeoIds[0].GeoId,
						listOfGeoIds[0].pointPos,
						listOfGeoIds[1].GeoId,
						listOfGeoIds[1].pointPos,
						listOfGeoIds[2].GeoId,
						listOfGeoIds[2].pointPos
					);
				}

				Obj->solve();
			}
		}
	};
	class HorizontalConstraint :public ConstraintCommand {
	public:
		HorizontalConstraint(QObject* parent) :ConstraintCommand(parent) {
		}
	protected:
		virtual void execute()override {
			SketcherObj* Obj = SketcherObjManager::instance().GetCurrentActiveSketcherObj();
			std::vector<SketcherObj::SelectGeoId> listOfGeoIds = currentSketchSelection();

			if (listOfGeoIds.size() == 1) {
				// horizontal line
				Obj->addConstraint(
						Sketcher::ConstraintType::Horizontal,
						listOfGeoIds[0].GeoId, Sketcher::PointPos::none
						);
				Obj->solve();
			}
			else if (listOfGeoIds.size() == 2) {
				// two points on the same horizontal line
				Obj->addConstraint(
					Sketcher::ConstraintType::Horizontal,
					listOfGeoIds[0].GeoId, listOfGeoIds[0].pointPos,
					listOfGeoIds[1].GeoId, listOfGeoIds[1].pointPos
				);
				Obj->solve();
			}
		}
	};
	class VerticalConstraint :public ConstraintCommand {
	public:
		VerticalConstraint(QObject* parent) :ConstraintCommand(parent) {
		}
	protected:
		virtual void execute()override {
			SketcherObj* Obj = SketcherObjManager::instance().GetCurrentActiveSketcherObj();
			std::vector<SketcherObj::SelectGeoId> listOfGeoIds = currentSketchSelection();
			if (listOfGeoIds.size() == 1) {
				// vertical line
				Obj->addConstraint(
					Sketcher::ConstraintType::Vertical,
					listOfGeoIds[0].GeoId, Sketcher::PointPos::none
				);
				Obj->solve();
			}
			else if (listOfGeoIds.size() == 2) {
				// two points on the same vertical line
				Obj->addConstraint(
					Sketcher::ConstraintType::Vertical,
					listOfGeoIds[0].GeoId, listOfGeoIds[0].pointPos,
					listOfGeoIds[1].GeoId, listOfGeoIds[1].pointPos
				);
				Obj->solve();
			}
		}
	};
	class ParallelConstraint :public ConstraintCommand {
	public:
		ParallelConstraint(QObject* parent) :ConstraintCommand(parent) {
		}
	protected:
		virtual void execute()override {
			SketcherObj* Obj = SketcherObjManager::instance().GetCurrentActiveSketcherObj();
			std::vector<SketcherObj::SelectGeoId> listOfGeoIds = currentSketchSelection();
			if (listOfGeoIds.size() == 2) {
				const Part::Geometry* g0 = Obj->resolveGeometry(listOfGeoIds[0].GeoId);
				const Part::Geometry* g1 = Obj->resolveGeometry(listOfGeoIds[1].GeoId);
				if (!g0 || !g1 || !g0->is<Part::GeomLineSegment>() || !g1->is<Part::GeomLineSegment>()) {
					CORE_ERROR("Parallel is only supported on lines");
					return;
				}
				Obj->addConstraint(
					Sketcher::ConstraintType::Parallel,
					listOfGeoIds[0].GeoId, Sketcher::PointPos::none,
					listOfGeoIds[1].GeoId, Sketcher::PointPos::none
				);

				Obj->solve();
			}
		}
	};
	class TangentConstraint :public ConstraintCommand {
	public:
		TangentConstraint(QObject* parent) :ConstraintCommand(parent) {
		}
	protected:
		virtual void execute()override {
			SketcherObj* Obj = SketcherObjManager::instance().GetCurrentActiveSketcherObj();
			std::vector<SketcherObj::SelectGeoId> listOfGeoIds = currentSketchSelection();
			if (listOfGeoIds.size() == 2) {
				const bool hasPoint0 = listOfGeoIds[0].pointPos != SketcherObj::PointPos::none;
				const bool hasPoint1 = listOfGeoIds[1].pointPos != SketcherObj::PointPos::none;
				Obj->addConstraint(
					Sketcher::ConstraintType::Tangent,
					listOfGeoIds[0].GeoId,
					hasPoint0 ? listOfGeoIds[0].pointPos : Sketcher::PointPos::none,
					listOfGeoIds[1].GeoId,
					hasPoint1 ? listOfGeoIds[1].pointPos : Sketcher::PointPos::none
				);

				Obj->solve();
			}
		}
	};
	class PerpendicularConstraint :public ConstraintCommand {
	public:
		PerpendicularConstraint(QObject* parent) :ConstraintCommand(parent) {
		}
	protected:
		virtual void execute()override {
			SketcherObj* Obj = SketcherObjManager::instance().GetCurrentActiveSketcherObj();
			std::vector<SketcherObj::SelectGeoId> listOfGeoIds = currentSketchSelection();
			if (listOfGeoIds.size() == 2) {
				const bool hasPoint0 = listOfGeoIds[0].pointPos != SketcherObj::PointPos::none;
				const bool hasPoint1 = listOfGeoIds[1].pointPos != SketcherObj::PointPos::none;
				if (!hasPoint0 && !hasPoint1) {
					const Part::Geometry* g0 = Obj->resolveGeometry(listOfGeoIds[0].GeoId);
					const Part::Geometry* g1 = Obj->resolveGeometry(listOfGeoIds[1].GeoId);
					if (!g0 || !g1 || !g0->is<Part::GeomLineSegment>() || !g1->is<Part::GeomLineSegment>()) {
						CORE_ERROR("Perpendicular is only supported on lines");
						return;
					}
				}
				Obj->addConstraint(
					Sketcher::ConstraintType::Perpendicular,
					listOfGeoIds[0].GeoId,
					hasPoint0 ? listOfGeoIds[0].pointPos : Sketcher::PointPos::none,
					listOfGeoIds[1].GeoId,
					hasPoint1 ? listOfGeoIds[1].pointPos : Sketcher::PointPos::none
				);

				Obj->solve();
			}
			else if (listOfGeoIds.size() == 3) {
				if (listOfGeoIds[2].pointPos == SketcherObj::PointPos::none) {
					//point and point and line
					Obj->addConstraint(
						Sketcher::ConstraintType::Perpendicular,
						listOfGeoIds[0].GeoId, listOfGeoIds[0].pointPos,
						listOfGeoIds[1].GeoId, listOfGeoIds[1].pointPos,
						listOfGeoIds[2].GeoId
					);
				}
				else
				{
					//this case need to figure out
					//line and line
					//Obj->addConstraint(
					//	Sketcher::ConstraintType::Perpendicular,
					//	listOfGeoIds[0].GeoId, listOfGeoIds[0].pointPos,
					//	listOfGeoIds[1].GeoId, listOfGeoIds[1].pointPos,
					//	listOfGeoIds[2].GeoId, listOfGeoIds[2].pointPos
					//);
				}

				Obj->solve();
			}
		}
	};
	class DistanceXConstraint :public ConstraintCommand {
	public:
		DistanceXConstraint(QObject* parent) :ConstraintCommand(parent) {
		}
	protected:
		virtual void execute()override {
			SketcherObj* Obj = SketcherObjManager::instance().GetCurrentActiveSketcherObj();
			std::vector<SketcherObj::SelectGeoId> listOfGeoIds = currentSketchSelection();

			int selectNum = listOfGeoIds.size();
			CORE_INFO("DistanceX execute: {} selected", selectNum);
			for (int k = 0; k < selectNum; ++k) {
				CORE_INFO(
					"  sel[{}] geo={} pos={}",
					k,
					listOfGeoIds[k].GeoId,
					static_cast<int>(listOfGeoIds[k].pointPos)
				);
			}
			// Ignore accidental edge/construction selections picked up by a
			// rubber-band: a point-to-point X distance only needs two points.
			if (selectNum > 2) {
				std::vector<SketcherObj::SelectGeoId> pointSel;
				for (const auto& sel : listOfGeoIds) {
					if (sel.pointPos != SketcherObj::PointPos::none) {
						pointSel.push_back(sel);
					}
				}
			if (pointSel.size() <= 2) {
					listOfGeoIds = pointSel;
					selectNum = static_cast<int>(pointSel.size());
				}
			}
			const double axisCur = signedAxisCurrent(Obj, listOfGeoIds, true);
			const double axisSign = axisCur < 0.0 ? -1.0 : 1.0;
			// GCS stores the difference as second - first, so the datum sign
			// must follow (second-first), otherwise the points would swap.
			const double diffSign = axisCur < 0.0 ? 1.0 : -1.0;
			if (selectNum>0) {
				ParamDialog dialog("DX");

				dialog.addParamDef(
					ParamDialog::ParamDef("DistanceX", -200, 200, std::fabs(axisCur))
				);
				dialog.setUp();
				int ret = dialog.exec();
				if (ret == QDialog::Accepted)
				{
					double distance = dialog.getParamValue("DistanceX");
					CORE_INFO("distanceX is {}", distance);
					if (selectNum == 1) {
						if (listOfGeoIds[0].pointPos == SketcherObj::PointPos::none) {
							//horizontal length
							auto newConstr = std::make_unique<Sketcher::Constraint>();
							newConstr->Type = Sketcher::ConstraintType::DistanceX;
							newConstr->First = listOfGeoIds[0].GeoId;
							newConstr->setValue(diffSign * distance);
							addOrSetDatumConstraint(Obj, std::move(newConstr));
						}
						else
						{
							// point on fixed x-coordinate
							auto newConstr = std::make_unique<Sketcher::Constraint>();
							newConstr->Type = Sketcher::ConstraintType::DistanceX;
							newConstr->First = listOfGeoIds[0].GeoId;
							newConstr->FirstPos = listOfGeoIds[0].pointPos;
							newConstr->setValue(axisSign * distance);
							addOrSetDatumConstraint(Obj, std::move(newConstr));
						}
					}
					else if (selectNum == 2) {
						// point to point horizontal distance
						auto newConstr = std::make_unique<Sketcher::Constraint>();
						newConstr->Type = Sketcher::ConstraintType::DistanceX;
						newConstr->First = listOfGeoIds[0].GeoId;
						newConstr->FirstPos = listOfGeoIds[0].pointPos;
						newConstr->Second = listOfGeoIds[1].GeoId;
						newConstr->SecondPos = listOfGeoIds[1].pointPos;
						newConstr->setValue(diffSign * distance);
						addOrSetDatumConstraint(Obj, std::move(newConstr));
					}
				}
			}
		}
	};
	class DistanceYConstraint :public ConstraintCommand {
	public:
		DistanceYConstraint(QObject* parent) :ConstraintCommand(parent) {
		}
	protected:
		virtual void execute()override {
			SketcherObj* Obj = SketcherObjManager::instance().GetCurrentActiveSketcherObj();
			std::vector<SketcherObj::SelectGeoId> listOfGeoIds = currentSketchSelection();

			int selectNum = listOfGeoIds.size();
			CORE_INFO("DistanceY execute: {} selected", selectNum);
			for (int k = 0; k < selectNum; ++k) {
				CORE_INFO(
					"  sel[{}] geo={} pos={}",
					k,
					listOfGeoIds[k].GeoId,
					static_cast<int>(listOfGeoIds[k].pointPos)
				);
			}
			if (selectNum > 2) {
				std::vector<SketcherObj::SelectGeoId> pointSel;
				for (const auto& sel : listOfGeoIds) {
					if (sel.pointPos != SketcherObj::PointPos::none) {
						pointSel.push_back(sel);
					}
				}
				if (pointSel.size() <= 2) {
					listOfGeoIds = pointSel;
					selectNum = static_cast<int>(pointSel.size());
				}
			}
			const double axisCur = signedAxisCurrent(Obj, listOfGeoIds, false);
			const double axisSign = axisCur < 0.0 ? -1.0 : 1.0;
			// GCS stores the difference as second - first.
			const double diffSign = axisCur < 0.0 ? 1.0 : -1.0;
			if (selectNum > 0) {
				ParamDialog dialog("DY");

				dialog.addParamDef(
					ParamDialog::ParamDef("DistanceY", -200, 200, std::fabs(axisCur))
				);
				dialog.setUp();
				int ret = dialog.exec();
				if (ret == QDialog::Accepted)
				{
					double distance = dialog.getParamValue("DistanceY");
					CORE_INFO("distanceY is {}", distance);
					if (selectNum == 1) {
						if (listOfGeoIds[0].pointPos == SketcherObj::PointPos::none) {
							//horizontal length
							auto newConstr = std::make_unique<Sketcher::Constraint>();
							newConstr->Type = Sketcher::ConstraintType::DistanceY;
							newConstr->First = listOfGeoIds[0].GeoId;
							newConstr->setValue(diffSign * distance);
							addOrSetDatumConstraint(Obj, std::move(newConstr));
						}
						else
						{
							// point on fixed x-coordinate
							auto newConstr = std::make_unique<Sketcher::Constraint>();
							newConstr->Type = Sketcher::ConstraintType::DistanceY;
							newConstr->First = listOfGeoIds[0].GeoId;
							newConstr->FirstPos = listOfGeoIds[0].pointPos;
							newConstr->setValue(axisSign * distance);
							addOrSetDatumConstraint(Obj, std::move(newConstr));
						}
					}
					else if (selectNum == 2) {
						// point to point horizontal distance
						auto newConstr = std::make_unique<Sketcher::Constraint>();
						newConstr->Type = Sketcher::ConstraintType::DistanceY;
						newConstr->First = listOfGeoIds[0].GeoId;
						newConstr->FirstPos = listOfGeoIds[0].pointPos;
						newConstr->Second = listOfGeoIds[1].GeoId;
						newConstr->SecondPos = listOfGeoIds[1].pointPos;
						newConstr->setValue(diffSign * distance);
						addOrSetDatumConstraint(Obj, std::move(newConstr));
					}
				}
			}
		}
	};
	class EqualConstraint :public ConstraintCommand {
	public:
		EqualConstraint(QObject* parent) :ConstraintCommand(parent) {
		}
	protected:
		virtual void execute()override {
			SketcherObj* Obj = SketcherObjManager::instance().GetCurrentActiveSketcherObj();
			std::vector<SketcherObj::SelectGeoId> listOfGeoIds = currentSketchSelection();

			int selectNum = listOfGeoIds.size();
			if (selectNum == 2) {
				if (listOfGeoIds[0].pointPos == SketcherObj::PointPos::none&& listOfGeoIds[1].pointPos == SketcherObj::PointPos::none) {
					Obj->addConstraint(
						Sketcher::ConstraintType::Equal,
						listOfGeoIds[0].GeoId, Sketcher::PointPos::none,
						listOfGeoIds[1].GeoId, Sketcher::PointPos::none
					);
					Obj->solve();
				}
			}
		}
	};
	class SymmetricConstraint :public ConstraintCommand {
	public:
		SymmetricConstraint(QObject* parent) :ConstraintCommand(parent) {
		}
	protected:
		virtual void execute()override {
			SketcherObj* Obj = SketcherObjManager::instance().GetCurrentActiveSketcherObj();
			std::vector<SketcherObj::SelectGeoId> listOfGeoIds = currentSketchSelection();

			int selectNum = listOfGeoIds.size();
			if (selectNum == 3) {
				if (listOfGeoIds[2].pointPos == SketcherObj::PointPos::none) {
					Obj->addConstraint(
						Sketcher::ConstraintType::Symmetric,
						listOfGeoIds[0].GeoId, listOfGeoIds[0].pointPos,
						listOfGeoIds[1].GeoId, listOfGeoIds[1].pointPos,
						listOfGeoIds[2].GeoId
					);
					Obj->solve();
				}
				else
				{
					Obj->addConstraint(
						Sketcher::ConstraintType::Symmetric,
						listOfGeoIds[0].GeoId, listOfGeoIds[0].pointPos,
						listOfGeoIds[1].GeoId, listOfGeoIds[1].pointPos,
						listOfGeoIds[2].GeoId, listOfGeoIds[2].pointPos
					);
					Obj->solve();
				}
			}
		}
	};
	class LengthConstraint :public ConstraintCommand {
	public:
		LengthConstraint(QObject* parent) :ConstraintCommand(parent) {
		}
	protected:
		virtual void execute()override {
			SketcherObj* Obj = SketcherObjManager::instance().GetCurrentActiveSketcherObj();
			std::vector<SketcherObj::SelectGeoId> listOfGeoIds = currentSketchSelection();
			int selectNum = listOfGeoIds.size();
			if (selectNum > 0) {
				ParamDialog dialog("Distance");
				dialog.addParamDef(
					ParamDialog::ParamDef(
						"Dist", 0, 1000, lengthCurrent(Obj, listOfGeoIds)
					)
				);
				dialog.setUp();
				int ret = dialog.exec();
				if (ret == QDialog::Accepted)
				{
					double distance = dialog.getParamValue("Dist");
					CORE_INFO("distance is {}", distance);
					if (selectNum == 1) {
						if (listOfGeoIds[0].pointPos == SketcherObj::PointPos::none) {
							//line or arc length
							auto newConstr = std::make_unique<Sketcher::Constraint>();
							newConstr->Type = Sketcher::ConstraintType::Distance;
							newConstr->First = listOfGeoIds[0].GeoId;
							newConstr->setValue(distance);
							addOrSetDatumConstraint(Obj, std::move(newConstr));
						}
					}
					else if (selectNum == 2) {
						// point to line horizontal distance. it needs to make sure Orientation
						if (listOfGeoIds[0].pointPos != SketcherObj::PointPos::none&&
							listOfGeoIds[1].pointPos == SketcherObj::PointPos::none
							) {
							auto newConstr = std::make_unique<Sketcher::Constraint>();
							newConstr->Type = Sketcher::ConstraintType::Distance;
							newConstr->First = listOfGeoIds[0].GeoId;
							newConstr->FirstPos = listOfGeoIds[0].pointPos;
							newConstr->Second = listOfGeoIds[1].GeoId;
							newConstr->setValue(distance);
							addOrSetDatumConstraint(Obj, std::move(newConstr));
						}
						else if 
							(listOfGeoIds[0].pointPos == SketcherObj::PointPos::none &&
							listOfGeoIds[1].pointPos == SketcherObj::PointPos::none
							) {
							// circle to circle, circle to
				            // arc, etc.
							auto newConstr = std::make_unique<Sketcher::Constraint>();
							newConstr->Type = Sketcher::ConstraintType::Distance;
							newConstr->First = listOfGeoIds[0].GeoId;
							newConstr->Second = listOfGeoIds[1].GeoId;
							newConstr->setValue(distance);
							addOrSetDatumConstraint(Obj, std::move(newConstr));
						}
						else if
							(listOfGeoIds[0].pointPos != SketcherObj::PointPos::none &&
								listOfGeoIds[1].pointPos != SketcherObj::PointPos::none
								) {
							// point to point distance
							auto newConstr = std::make_unique<Sketcher::Constraint>();
							newConstr->Type = Sketcher::ConstraintType::Distance;
							newConstr->First = listOfGeoIds[0].GeoId;
							newConstr->FirstPos = listOfGeoIds[0].pointPos;
							newConstr->Second = listOfGeoIds[1].GeoId;
							newConstr->SecondPos = listOfGeoIds[1].pointPos;
							newConstr->setValue(distance);
							addOrSetDatumConstraint(Obj, std::move(newConstr));
						}
					}
				}
			}
		}
	};
	class RadiusConstraint :public ConstraintCommand {
	public:
		RadiusConstraint(QObject* parent) :ConstraintCommand(parent) {
		}
	protected:
		virtual void execute()override {
			SketcherObj* Obj = SketcherObjManager::instance().GetCurrentActiveSketcherObj();
			std::vector<SketcherObj::SelectGeoId> listOfGeoIds = currentSketchSelection();

			int selectNum = listOfGeoIds.size();
			if (selectNum > 0) {
				ParamDialog dialog("Radius");

				dialog.addParamDef(
					ParamDialog::ParamDef(
						"Radius", 0, 1000, radiusCurrent(Obj, listOfGeoIds[0])
					)
				);
				dialog.setUp();
				int ret = dialog.exec();
				if (ret == QDialog::Accepted)
				{
					double distance = dialog.getParamValue("Radius");
					CORE_INFO("Radius is {}", distance);
					if (selectNum == 1) {
						if (listOfGeoIds[0].pointPos == SketcherObj::PointPos::none) {
							//horizontal length
							auto newConstr = std::make_unique<Sketcher::Constraint>();
							newConstr->Type = Sketcher::ConstraintType::Radius;
							newConstr->First = listOfGeoIds[0].GeoId;
							newConstr->setValue(distance);
							addOrSetDatumConstraint(Obj, std::move(newConstr));
						}
					}
				}
			}
		}
	};
	class DiameterConstraint :public ConstraintCommand {
	public:
		DiameterConstraint(QObject* parent) :ConstraintCommand(parent) {
		}
	protected:
		virtual void execute()override {
			SketcherObj* Obj = SketcherObjManager::instance().GetCurrentActiveSketcherObj();
			std::vector<SketcherObj::SelectGeoId> listOfGeoIds = currentSketchSelection();

			int selectNum = listOfGeoIds.size();
			if (selectNum > 0) {
				ParamDialog dialog("Diameter");

				dialog.addParamDef(
					ParamDialog::ParamDef(
						"Diameter", 0, 1000, 2.0 * radiusCurrent(Obj, listOfGeoIds[0])
					)
				);
				dialog.setUp();
				int ret = dialog.exec();
				if (ret == QDialog::Accepted)
				{
					double distance = dialog.getParamValue("Diameter");
					CORE_INFO("Diameter is {}", distance);
					if (selectNum == 1) {
						if (listOfGeoIds[0].pointPos == SketcherObj::PointPos::none) {
							//horizontal length
							auto newConstr = std::make_unique<Sketcher::Constraint>();
							newConstr->Type = Sketcher::ConstraintType::Diameter;
							newConstr->First = listOfGeoIds[0].GeoId;
							newConstr->setValue(distance);
							addOrSetDatumConstraint(Obj, std::move(newConstr));
						}
					}
				}
			}
		}
	};
	class BlockConstraint :public ConstraintCommand {
	public:
		BlockConstraint(QObject* parent) :ConstraintCommand(parent) {
		}
	protected:
		virtual void execute()override {
			SketcherObj* Obj = SketcherObjManager::instance().GetCurrentActiveSketcherObj();
			std::vector<SketcherObj::SelectGeoId> listOfGeoIds = currentSketchSelection();
			for (int i = 0; i < listOfGeoIds.size(); i++) {
				if (listOfGeoIds[i].pointPos == SketcherObj::PointPos::none) {
					Obj->addConstraint(
						Sketcher::ConstraintType::Block,
						listOfGeoIds[i].GeoId, Sketcher::PointPos::none
					);
					Obj->solve();
				}
			}
		}
	};
	class AngleConstraint :public ConstraintCommand {
	public:
		AngleConstraint(QObject* parent) :ConstraintCommand(parent) {
		}
	protected:
		virtual void execute()override {
			SketcherObj* Obj = SketcherObjManager::instance().GetCurrentActiveSketcherObj();
			std::vector<SketcherObj::SelectGeoId> listOfGeoIds = currentSketchSelection();

			int selectNum = listOfGeoIds.size();
			if (selectNum > 0) {
				// A pair of lines is measured the way the constraint will hold it: from
				// the end of each line that points away from the corner, with the two
				// ends stored on the constraint. Taking the lines' raw directions
				// instead gave the supplement whenever a line ran the other way, and
				// the solver then had to flip the line to satisfy the value - which is
				// a constraint the user did not mean and, when it cannot be reached,
				// one that fails to solve.
				SketcherObj::MeasuredAngle pair;
				bool pairAngle = false;
				if (selectNum == 2) {
					pair = Obj->measureAngleBetweenLines(
						listOfGeoIds[0].GeoId,
						listOfGeoIds[1].GeoId,
						listOfGeoIds[0].pointPos,
						listOfGeoIds[1].pointPos
					);
					pairAngle = pair.usable;
					if (!pairAngle) {
						CORE_WARN(
							"[Constraint] angle: the two picked elements are not two lines "
							"that meet, so there is no angle to constrain");
						return;
					}
				}
				ParamDialog dialog("Angle");
				dialog.addParamDef(
					ParamDialog::ParamDef(
						"Angle", 0, 360,
						pairAngle ? pair.radians * 180.0 / 3.14159265358979323846
								  : angleCurrentDeg(Obj, listOfGeoIds)
					)
				);
				dialog.setUp();
				int ret = dialog.exec();
				if (ret == QDialog::Accepted)
				{
					double distance = dialog.getParamValue("Angle");
					
					CORE_INFO("angle is {}", distance);
					distance = distance * 3.14159265358979323846f / 180.0f;
					if (pairAngle) {
						auto newConstr = std::make_unique<Sketcher::Constraint>();
						newConstr->Type = Sketcher::ConstraintType::Angle;
						newConstr->First = pair.firstGeoId;
						newConstr->FirstPos = pair.firstPos;
						newConstr->Second = pair.secondGeoId;
						newConstr->SecondPos = pair.secondPos;
						newConstr->setValue(distance);
						addOrSetDatumConstraint(Obj, std::move(newConstr));
					}
					else if (selectNum == 1) {
						if (
							listOfGeoIds[0].pointPos == SketcherObj::PointPos::none
							) {
							auto newConstr = std::make_unique<Sketcher::Constraint>();
							newConstr->Type = Sketcher::ConstraintType::Angle;
							newConstr->First = listOfGeoIds[0].GeoId;
							newConstr->setValue(distance);
							addOrSetDatumConstraint(Obj, std::move(newConstr));
						}
					}
				}
			}
		}
	};
	void AddConstraintGroups(QToolBar* p_toolbar, QObject* p_owner)
	{
		auto* coincident = new CoincidentConstraint(p_owner);
		coincident->setIcon(":/widgets/icons/constraint/Constraint_Coincident.svg");
		auto* horizontal = new HorizontalConstraint(p_owner);
		horizontal->setIcon(":/widgets/icons/constraint/Constraint_Horizontal.svg");
		auto* vertical = new VerticalConstraint(p_owner);
		vertical->setIcon(":/widgets/icons/constraint/Constraint_Vertical.svg");
		auto* parallel = new ParallelConstraint(p_owner);
		parallel->setIcon(":/widgets/icons/constraint/Constraint_Parallel.svg");
		auto* perpendicular = new PerpendicularConstraint(p_owner);
		perpendicular->setIcon(":/widgets/icons/constraint/Constraint_Perpendicular.svg");
		auto* distanceX = new DistanceXConstraint(p_owner);
		distanceX->setIcon(":/widgets/icons/constraint/Constraint_DistanceX.svg");
		auto* distanceY = new DistanceYConstraint(p_owner);
		distanceY->setIcon(":/widgets/icons/constraint/Constraint_VerticalDistance.svg");
		auto* tangent = new TangentConstraint(p_owner);
		tangent->setIcon(":/widgets/icons/constraint/Constraint_Tangent.svg");
		auto* equal = new EqualConstraint(p_owner);
		equal->setIcon(":/widgets/icons/constraint/Constraint_EqualLength.svg");
		auto* symmetric = new SymmetricConstraint(p_owner);
		symmetric->setIcon(":/widgets/icons/constraint/Constraint_Symmetric.svg");
		auto* length = new LengthConstraint(p_owner);
		length->setIcon(":/widgets/icons/constraint/Constraint_Length.svg");
		auto* radius = new RadiusConstraint(p_owner);
		radius->setIcon(":/widgets/icons/constraint/Constraint_Radius.svg");
		auto* diameter = new DiameterConstraint(p_owner);
		diameter->setIcon(":/widgets/icons/constraint/Constraint_Diameter.svg");
		auto* block = new BlockConstraint(p_owner);
		block->setIcon(":/widgets/icons/constraint/Constraint_Block.svg");
		auto* angle = new AngleConstraint(p_owner);
		angle->setIcon(":/widgets/icons/constraint/Constraint_InternalAngle.svg");
		auto* smartDimension = new SmartDimensionCommand(p_owner);
		smartDimension->setIcon(":/widgets/icons/constraint/Constraint_Dimension.svg");

		const auto addGroup = [p_toolbar](const QString& p_title, int p_columns,
			std::initializer_list<QAction*> p_actions) {
			auto* group = new RibbonGroup(p_title, p_toolbar);
			for (auto* action : p_actions) {
				group->addAction(action);
			}
			group->setGrid(p_columns, 2);
			QAction* groupAction = p_toolbar->addWidget(group);
			groupAction->setText(p_title);
			return group;
		};
		// Geometric constraints first, then the ones that carry a value. A sketch
		// usually gets a few of the former before any of the latter.
		addGroup("几何约束", 5, { coincident->action(), horizontal->action(), vertical->action(),
			parallel->action(), perpendicular->action(), tangent->action(),
			equal->action(), symmetric->action(), block->action() });
		RibbonGroup* lastGroup = addGroup("尺寸约束", 4, { distanceX->action(),
			distanceY->action(), length->action(), radius->action(),
			diameter->action(), angle->action(), smartDimension->action() });
		lastGroup->setDrawSeparator(false);

		const auto setLabel = [](QAction* p_action, const QString& p_text, const QString& p_english) {
			p_action->setText(p_text);
			p_action->setToolTip(QStringLiteral("%1 (%2)").arg(p_text, p_english));
		};
		setLabel(coincident->action(), QStringLiteral("重合"), QStringLiteral("Coincident"));
		setLabel(horizontal->action(), QStringLiteral("水平"), QStringLiteral("Horizontal"));
		setLabel(vertical->action(), QStringLiteral("竖直"), QStringLiteral("Vertical"));
		setLabel(parallel->action(), QStringLiteral("平行"), QStringLiteral("Parallel"));
		setLabel(perpendicular->action(), QStringLiteral("垂直"), QStringLiteral("Perpendicular"));
		setLabel(tangent->action(), QStringLiteral("相切"), QStringLiteral("Tangent"));
		setLabel(equal->action(), QStringLiteral("相等"), QStringLiteral("Equal"));
		setLabel(symmetric->action(), QStringLiteral("对称"), QStringLiteral("Symmetric"));
		setLabel(block->action(), QStringLiteral("锁定"), QStringLiteral("Block"));
		setLabel(distanceX->action(), QStringLiteral("水平距离"), QStringLiteral("DistanceX"));
		setLabel(distanceY->action(), QStringLiteral("竖直距离"), QStringLiteral("DistanceY"));
		setLabel(length->action(), QStringLiteral("长度"), QStringLiteral("Length"));
		setLabel(radius->action(), QStringLiteral("半径"), QStringLiteral("Radius"));
		setLabel(diameter->action(), QStringLiteral("直径"), QStringLiteral("Diameter"));
		setLabel(angle->action(), QStringLiteral("角度"), QStringLiteral("Angle"));
		setLabel(smartDimension->action(), QStringLiteral("尺寸"), QStringLiteral("Dimension"));
	}
}

