#include "editor/Toolbar/ContraintToolbar.h"
#include "editor/Command/command.h"
#include "Core/Global/ServiceLocator.h"
#include "renderer/SceneView.h"
#include "Sketcher/SketcherObjManager.h"
#include "Sketcher/SketcherObj.h"
#include "Geometry.h"
#include "core/log.h"
#include <QCoreApplication>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QFormLayout>
#include <cmath>
namespace MOON {
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
		if (sel.size() == 2) {
			Base::Vector2d a1, a2, b1, b2;
			if (lineEnds2d(obj, sel[0].GeoId, a1, a2)
				&& lineEnds2d(obj, sel[1].GeoId, b1, b2)) {
				const double cross = (a2.x - a1.x) * (b2.y - b1.y)
					- (a2.y - a1.y) * (b2.x - b1.x);
				const double dot = (a2.x - a1.x) * (b2.x - b1.x)
					+ (a2.y - a1.y) * (b2.y - b1.y);
				return std::fabs(std::atan2(cross, dot) * 180.0 / pi);
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
				CORE_WARN("add constraint failed, solver error {}", err);
			}
			else {
				CORE_INFO("solve ok after add constraint id {}", id);
			}
		}
	}
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
			std::vector<SketcherObj::SelectGeoId> listOfGeoIds = Obj->getSelectGeoPosIds();

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
			std::vector<SketcherObj::SelectGeoId> listOfGeoIds = Obj->getSelectGeoPosIds();

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
			std::vector<SketcherObj::SelectGeoId> listOfGeoIds = Obj->getSelectGeoPosIds();
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
			std::vector<SketcherObj::SelectGeoId> listOfGeoIds = Obj->getSelectGeoPosIds();
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
			std::vector<SketcherObj::SelectGeoId> listOfGeoIds = Obj->getSelectGeoPosIds();
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
			std::vector<SketcherObj::SelectGeoId> listOfGeoIds = Obj->getSelectGeoPosIds();
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
			std::vector<SketcherObj::SelectGeoId> listOfGeoIds = Obj->getSelectGeoPosIds();

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
			std::vector<SketcherObj::SelectGeoId> listOfGeoIds = Obj->getSelectGeoPosIds();

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
			std::vector<SketcherObj::SelectGeoId> listOfGeoIds = Obj->getSelectGeoPosIds();

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
			std::vector<SketcherObj::SelectGeoId> listOfGeoIds = Obj->getSelectGeoPosIds();

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
			std::vector<SketcherObj::SelectGeoId> listOfGeoIds = Obj->getSelectGeoPosIds();
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
			std::vector<SketcherObj::SelectGeoId> listOfGeoIds = Obj->getSelectGeoPosIds();

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
			std::vector<SketcherObj::SelectGeoId> listOfGeoIds = Obj->getSelectGeoPosIds();

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
			std::vector<SketcherObj::SelectGeoId> listOfGeoIds = Obj->getSelectGeoPosIds();
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
			std::vector<SketcherObj::SelectGeoId> listOfGeoIds = Obj->getSelectGeoPosIds();

			int selectNum = listOfGeoIds.size();
			if (selectNum > 0) {
				ParamDialog dialog("Angle");
				dialog.addParamDef(
					ParamDialog::ParamDef(
						"Angle", 0, 360, angleCurrentDeg(Obj, listOfGeoIds)
					)
				);
				dialog.setUp();
				int ret = dialog.exec();
				if (ret == QDialog::Accepted)
				{
					double distance = dialog.getParamValue("Angle");
					
					CORE_INFO("angle is {}", distance);
					distance = distance * 3.14159265358979323846f / 180.0f;
					if (selectNum == 3) {
						if (
							listOfGeoIds[0].pointPos != SketcherObj::PointPos::none
							&& listOfGeoIds[1].pointPos != SketcherObj::PointPos::none
							&& listOfGeoIds[2].pointPos != SketcherObj::PointPos::none
							) {
						    
							//auto newConstr = std::make_unique<Sketcher::Constraint>();
							//newConstr->Type = Sketcher::ConstraintType::Angle;
							//newConstr->First = listOfGeoIds[0].GeoId;
							//newConstr->FirstPos = listOfGeoIds[0].pointPos;
							//newConstr->Second = listOfGeoIds[1].GeoId;
							//newConstr->SecondPos = listOfGeoIds[1].pointPos;
							//newConstr->Third = listOfGeoIds[2].GeoId;
							//newConstr->ThirdPos = listOfGeoIds[2].pointPos;
							//newConstr->setValue(distance);
							//Obj->addConstraint(std::move(newConstr));
							//Obj->solve();
						}
					}
					else if (selectNum == 2) {
						// line to line
						if (
							listOfGeoIds[0].pointPos != SketcherObj::PointPos::none
							&& listOfGeoIds[1].pointPos != SketcherObj::PointPos::none
							) {

							auto newConstr = std::make_unique<Sketcher::Constraint>();
							newConstr->Type = Sketcher::ConstraintType::Angle;
							newConstr->First = listOfGeoIds[0].GeoId;
							newConstr->FirstPos = listOfGeoIds[0].pointPos;
							newConstr->Second = listOfGeoIds[1].GeoId;
							newConstr->SecondPos = listOfGeoIds[1].pointPos;
							newConstr->setValue(distance);
							addOrSetDatumConstraint(Obj, std::move(newConstr));
						}
						else if (listOfGeoIds[0].pointPos == SketcherObj::PointPos::none
							&& listOfGeoIds[1].pointPos == SketcherObj::PointPos::none) {
							auto newConstr = std::make_unique<Sketcher::Constraint>();
							newConstr->Type = Sketcher::ConstraintType::Angle;
							newConstr->First = listOfGeoIds[0].GeoId;
							newConstr->Second = listOfGeoIds[1].GeoId;
							newConstr->setValue(distance);
							addOrSetDatumConstraint(Obj, std::move(newConstr));
						}
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
	class ConstraintToolbar::Internal {
	public:
		Internal(ConstraintToolbar* toolbar) :self(toolbar)
		{
			setup();
		}
		void setup() {
			coincident = new CoincidentConstraint(self);
			coincident->setIcon(":/widgets/icons/constraint/Constraint_Coincident.svg");
			horizontal = new HorizontalConstraint(self);
			horizontal->setIcon(":/widgets/icons/constraint/Constraint_Horizontal.svg");
			vertical = new VerticalConstraint(self);
			vertical->setIcon(":/widgets/icons/constraint/Constraint_Vertical.svg");
			parallel = new ParallelConstraint(self);
			parallel->setIcon(":/widgets/icons/constraint/Constraint_Parallel.svg");
			perpendicular = new PerpendicularConstraint(self);
			perpendicular->setIcon(":/widgets/icons/constraint/Constraint_Perpendicular.svg");
			distanceX = new DistanceXConstraint(self);
			distanceX->setIcon(":/widgets/icons/constraint/Constraint_DistanceX.svg");
			distanceY = new DistanceYConstraint(self);
			distanceY->setIcon(":/widgets/icons/constraint/Constraint_VerticalDistance.svg");
			tangent = new TangentConstraint(self);
			tangent->setIcon(":/widgets/icons/constraint/Constraint_Tangent.svg");
			equal = new EqualConstraint(self);
			equal->setIcon(":/widgets/icons/constraint/Constraint_EqualLength.svg");
			symmetric = new SymmetricConstraint(self);
			symmetric->setIcon(":/widgets/icons/constraint/Constraint_Symmetric.svg");
			length = new LengthConstraint(self);
			length->setIcon(":/widgets/icons/constraint/Constraint_Length.svg");
			radius = new RadiusConstraint(self);
			radius->setIcon(":/widgets/icons/constraint/Constraint_Radius.svg");
			diameter = new DiameterConstraint(self);
			diameter->setIcon(":/widgets/icons/constraint/Constraint_Diameter.svg");
			block = new BlockConstraint(self);
			block->setIcon(":/widgets/icons/constraint/Constraint_Block.svg");
			angle= new AngleConstraint(self);
			angle->setIcon(":/widgets/icons/constraint/Constraint_InternalAngle.svg");
			self->addAction(coincident->action());
			self->addAction(horizontal->action());
			self->addAction(vertical->action());
			self->addAction(parallel->action());
			self->addAction(perpendicular->action());
			self->addAction(tangent->action());
			self->addAction(distanceX->action());
			self->addAction(distanceY->action());
			self->addAction(equal->action());
			self->addAction(symmetric->action());
			self->addAction(length->action());
			self->addAction(radius->action());
			self->addAction(diameter->action());
			self->addAction(block->action());
			self->addAction(angle->action());
			retranslateUi();
		}
		void retranslateUi() {
			coincident->action()->setText(QCoreApplication::translate("ConstraintToolbar", "Coincident", nullptr));
			horizontal->action()->setText(QCoreApplication::translate("ConstraintToolbar", "Horizontal", nullptr));
			vertical->action()->setText(QCoreApplication::translate("ConstraintToolbar", "Vertical", nullptr));
			parallel->action()->setText(QCoreApplication::translate("ConstraintToolbar", "Parallel", nullptr));
			perpendicular->action()->setText(QCoreApplication::translate("ConstraintToolbar", "Perpendicular", nullptr));
			distanceX->action()->setText(QCoreApplication::translate("ConstraintToolbar", "DistanceX", nullptr));
			distanceY->action()->setText(QCoreApplication::translate("ConstraintToolbar", "DistanceY", nullptr));
			tangent->action()->setText(QCoreApplication::translate("ConstraintToolbar", "Tangent", nullptr));
			equal->action()->setText(QCoreApplication::translate("ConstraintToolbar", "Equal", nullptr));
			symmetric->action()->setText(QCoreApplication::translate("ConstraintToolbar", "Symmetric", nullptr));
			length->action()->setText(QCoreApplication::translate("ConstraintToolbar", "Length", nullptr));
			radius->action()->setText(QCoreApplication::translate("ConstraintToolbar", "Radius", nullptr));
			diameter->action()->setText(QCoreApplication::translate("ConstraintToolbar", "Diameter", nullptr));
			block->action()->setText(QCoreApplication::translate("ConstraintToolbar", "Block", nullptr));
			angle->action()->setText(QCoreApplication::translate("ConstraintToolbar", "Angle", nullptr));
		}
	private:
		friend class ConstraintToolbar;
		ConstraintToolbar* self = nullptr;
		ConstraintCommand* coincident = nullptr;
		ConstraintCommand* horizontal = nullptr;
		ConstraintCommand* vertical = nullptr;
		ConstraintCommand* parallel = nullptr;
		ConstraintCommand* perpendicular = nullptr;
		ConstraintCommand* distanceX = nullptr;
		ConstraintCommand* distanceY = nullptr;
		ConstraintCommand* tangent = nullptr;
		ConstraintCommand* equal = nullptr;
		ConstraintCommand* symmetric = nullptr;
		ConstraintCommand* length = nullptr;
		ConstraintCommand* radius = nullptr;
		ConstraintCommand* diameter = nullptr;
		ConstraintCommand* block = nullptr;
		ConstraintCommand* angle = nullptr;
	};

	ConstraintToolbar::ConstraintToolbar(const QString& title, QWidget* parent)
		:QToolBar(title, parent),mInternal(new Internal(this))
	{
		RegService(ConstraintToolbar,*this);
	}
	ConstraintToolbar::ConstraintToolbar(QWidget* parentObject) 
		:QToolBar(parentObject), mInternal(new Internal(this))
	{
		RegService(ConstraintToolbar, *this);
	}
	ConstraintToolbar::~ConstraintToolbar()
	{
		if (mInternal) {
			delete mInternal;
			mInternal = nullptr;
		}
	}
}
