#include "editor/Toolbar/sketchToolbar.h"
#include "editor/Toolbar/RibbonGroup.h"
#include "editor/Toolbar/ConstraintGroups.h"
#include "editor/Command/command.h"
#include "Core/Global/ServiceLocator.h"
#include "renderer/SceneView.h"
#include "renderer/GizmoRenderPass.h"
#include "Sketcher/SketcherObjManager.h"
#include "Sketcher/SketcherObj.h"
#include "Interactive/Widgets/DrawSketchHandlerExternalGeometry.h"
#include "core/log.h"
#include <QCoreApplication>
#include <algorithm>
#include <initializer_list>
#include <vector>
namespace MOON {

	namespace
	{
		/** Switches the external geometry tool widget off. Used wherever the tool has
		 * to be left behind: another tool takes the clicks, the sketch is left, or
		 * Escape is pressed inside the tool itself. */
		void DisableExternalGeometryTool()
		{
			auto& view = GetService(Editor::Panels::SceneView);
			auto& gizmoPass
				= view.GetRenderer().GetPass<Editor::Rendering::GizmoRenderPass>("ImRenderer");
			auto* tool = dynamic_cast<DrawSketchHandlerExternalGeometry*>(
				gizmoPass.getGizmoWidget(DrawSketchHandlerExternalGeometry::WidgetName));
			if (tool != nullptr && tool->isActived()) {
				tool->setActive(false);
			}
		}
	}

	class  CreateCurveCommand : public Command
	{
	public:
		CreateCurveCommand(QObject* parent,const std::string& handler) :Command(parent),handlerName(handler) {
			auto action = new QAction(this);
			action->setCheckable(true);
			setAction(action);
			commandMap[handler] = this;
		}
		static std::vector<std::string> blackList; 
		static std::unordered_map<std::string, CreateCurveCommand*> commandMap;
	protected:
		virtual void execute()override {
			bool value = action()->isChecked();
			auto& view = GetService(Editor::Panels::SceneView);
			
			auto& gizmoPass = view.GetRenderer().GetPass<Editor::Rendering::GizmoRenderPass>("ImRenderer");
			gizmoPass.enableGizmoWidget(handlerName, value);
			if (value) {
				for (int i = 0;i < blackList.size();i++) {
					if (blackList[i] != handlerName) {
						gizmoPass.enableGizmoWidget(blackList[i], false);
						if (commandMap.find(blackList[i]) != commandMap.end()) {
							commandMap[blackList[i]]->action()->setChecked(false);
						}
					}
				}
			}
		}
	private:
		std::string handlerName = "";
			 
	};
	std::vector<std::string> CreateCurveCommand::blackList = {
		"DrawSketchHandlerPoint",
		"DrawSketchHandlerLine",
		"DrawSketchHandlerLineSet",
		"DrawSketchHandlerCircle",
		"DrawSketchHandlerArc",
		"DrawSketchHandlerArcSlot",
		"DrawSketchHandlerEllipse",
		"DrawSketchHandlerBSpline",
		"DrawSketchHandlerRectangle" ,
		"DrawSketchHandlerPolygon",
		"DrawSketchHandlerSlot",
		"DrawSketchHandlerRotate",
		"DrawSketchHandlerTranslate",
		"DrawSketchHandlerSymmetry",
		"DrawSketchHandlerTrimming",
		"DrawSketchHandlerFillet",
		"DrawSketchHandlerOffset"
	};
	std::unordered_map<std::string, CreateCurveCommand*> CreateCurveCommand::commandMap;

	/** Turns the add-external-geometry tool on and off.
	 *
	 * The tool is a draw handler of its own (DrawSketchHandlerExternalGeometry):
	 * it picks a sub-shape of another feature, projects it into the sketch plane -
	 * or cuts it with that plane - and hands the resulting curves to the sketch
	 * (SketcherObj::addExternalGeometry). Because it is a handler it is exclusive
	 * with the drawing tools, as they all want the same clicks.
	 *
	 * The two commands are the two flavours of that one widget: the reference is
	 * either the projection of the picked shape or the section of it with the sketch
	 * plane. They therefore switch each other off, and both switch the same widget
	 * on - only its mode differs. */
	class AddExternalGeometryCommand : public Command
	{
	public:
		AddExternalGeometryCommand(QObject* parent, bool intersection)
			:Command(parent), m_intersection(intersection) {
			auto action = new QAction(this);
			action->setCheckable(true);
			setAction(action);
			s_commands.push_back(this);
		}
	private:
		bool m_intersection = false;
		static std::vector<AddExternalGeometryCommand*> s_commands;
	public:
		static const std::vector<AddExternalGeometryCommand*>& commands() {
			return s_commands;
		}
	protected:
		virtual void execute()override {
			const bool value = action()->isChecked();
			auto& view = GetService(Editor::Panels::SceneView);
			auto& gizmoPass
				= view.GetRenderer().GetPass<Editor::Rendering::GizmoRenderPass>("ImRenderer");
			auto* tool = dynamic_cast<DrawSketchHandlerExternalGeometry*>(
				gizmoPass.getGizmoWidget(DrawSketchHandlerExternalGeometry::WidgetName));
			if (SketcherObjManager::instance().GetCurrentActiveSketcherObj() == nullptr
				|| tool == nullptr) {
				CORE_WARN("[ExternalGeo] no sketch is being edited");
				action()->setChecked(false);
				return;
			}
			if (value) {
				// The two flavours are one mode, so the other button has to come up.
				for (AddExternalGeometryCommand* other : s_commands) {
					if (other != this && other->action()->isChecked()) {
						other->action()->setChecked(false);
					}
				}
				// One tool at a time: leave whatever draw handler was running.
				for (int i = 0; i < static_cast<int>(CreateCurveCommand::blackList.size()); i++) {
					gizmoPass.enableGizmoWidget(CreateCurveCommand::blackList[i], false);
					if (CreateCurveCommand::commandMap.find(CreateCurveCommand::blackList[i])
						!= CreateCurveCommand::commandMap.end()) {
						CreateCurveCommand::commandMap[CreateCurveCommand::blackList[i]]
							->action()->setChecked(false);
					}
				}
			}
			tool->setMode(
				m_intersection
					? DrawSketchHandlerExternalGeometry::EMode::Section
					: DrawSketchHandlerExternalGeometry::EMode::Projection
			);
			tool->setActive(value);
		}
	};
	std::vector<AddExternalGeometryCommand*> AddExternalGeometryCommand::s_commands;

	/** Switches the selected curves between defining geometry and construction
	 * geometry - FreeCAD's Toggle Construction Geometry button.
	 *
	 * A construction curve stays in the sketch and in the solver but is never part
	 * of the shape the sketch produces, so it is a reference the rest of the sketch
	 * is constrained against (SketcherObj::setConstruction).
	 *
	 * FreeCAD's button has a second half - with nothing selected it switches the
	 * mode the *next* curves are created in - which this one does not: every drawing
	 * tool hands the flag for the geometry it creates itself, so that mode would
	 * have to be plumbed through all of them. */
	class ToggleConstructionCommand : public Command
	{
	public:
		ToggleConstructionCommand(QObject* parent) :Command(parent) {
			auto action = new QAction(this);
			setAction(action);
		}
	protected:
		virtual void execute()override {
			SketcherObj* sketch
				= SketcherObjManager::instance().GetCurrentActiveSketcherObj();
			if (sketch == nullptr) {
				CORE_WARN("[ToggleConstruction] no sketch is being edited");
				return;
			}
			// The same curve can be selected as a curve and through one of its
			// markers: toggling it once per selection entry would leave it as it was.
			std::vector<int> geoIds;
			for (int geoId : sketch->getSelectIds()) {
				if (geoId < 0) {
					continue;  // an external reference is not the sketch's own curve
				}
				if (std::find(geoIds.begin(), geoIds.end(), geoId) == geoIds.end()) {
					geoIds.push_back(geoId);
				}
			}
			if (geoIds.empty()) {
				// The selection is what says which curves are meant.
				CORE_WARN("[ToggleConstruction] no curve is selected");
				return;
			}
			for (int geoId : geoIds) {
				if (sketch->isInternalGeometry(geoId)) {
					// The axes and the focuses of an ellipse are construction geometry for
					// a reason: they are drawn and can be constrained against, but they are
					// not part of the sketch's shape. FreeCAD refuses the same switch.
					CORE_WARN(
						"[ToggleConstruction] curve {0} is the internal geometry of curve {1}; "
						"it stays construction",
						geoId,
						sketch->internalGeometryOwner(geoId));
					continue;
				}
				sketch->setConstruction(geoId, !sketch->isConstructionGeometry(geoId));
			}
		}
	};

	class SketchToolbar::SketchToolbarInternal {
	public:

		SketchToolbarInternal(SketchToolbar* toolbar) :self(toolbar)
		{
			
		}
		void setup() {
			point = new CreateCurveCommand(self, "DrawSketchHandlerPoint");
			line =new CreateCurveCommand(self, "DrawSketchHandlerLine");
			lineSet = new CreateCurveCommand(self,"DrawSketchHandlerLineSet");
			circle = new CreateCurveCommand(self, "DrawSketchHandlerCircle");
			arc = new CreateCurveCommand(self, "DrawSketchHandlerArc");
			arcSlot = new CreateCurveCommand(self, "DrawSketchHandlerArcSlot");
			ellipse = new CreateCurveCommand(self, "DrawSketchHandlerEllipse");
			bspline = new CreateCurveCommand(self, "DrawSketchHandlerBSpline");
			rectangle = new CreateCurveCommand(self,"DrawSketchHandlerRectangle");
			polygon = new CreateCurveCommand(self, "DrawSketchHandlerPolygon");
			slot = new CreateCurveCommand(self, "DrawSketchHandlerSlot");
			trimming = new CreateCurveCommand(self, "DrawSketchHandlerTrimming");
			rotate = new CreateCurveCommand(self, "DrawSketchHandlerRotate");
			translate = new CreateCurveCommand(self, "DrawSketchHandlerTranslate");
			symmetry=new CreateCurveCommand(self, "DrawSketchHandlerSymmetry");
			fillet = new CreateCurveCommand(self, "DrawSketchHandlerFillet");
			offset = new CreateCurveCommand(self, "DrawSketchHandlerOffset");
			toggleConstruction = new ToggleConstructionCommand(self);
			external = new AddExternalGeometryCommand(self, /*intersection*/ false);
			externalIntersection = new AddExternalGeometryCommand(self, /*intersection*/ true);
			point->setIcon(":/widgets/icons/Sketcher_CreatePoint.svg");
		    line->setIcon(":/widgets/icons/Sketcher_CreateLine.svg");
			lineSet->setIcon(":/widgets/icons/Sketcher_CreatePolyline.svg");
			circle->setIcon(":/widgets/icons/Sketcher_CreateCircle.svg");
			arc->setIcon(":/widgets/icons/Sketcher_CreateArc.svg");
			arcSlot->setIcon(":/widgets/icons/Sketcher_CreateArcSlot.svg");
			ellipse->setIcon(":/widgets/icons/Sketcher_CreateEllipseByCenter.svg");
			bspline->setIcon(":/widgets/icons/Sketcher_CreateBSpline.svg");
			rectangle->setIcon(":/widgets/icons/Sketcher_CreateRectangle_Constr.svg");
			polygon->setIcon(":/widgets/icons/Sketcher_CreateRegularPolygon.svg");
			slot->setIcon(":/widgets/icons/Sketcher_CreateSlot.svg");
			trimming->setIcon(":/widgets/icons/Sketcher_Trimming.svg");
			rotate->setIcon(":/widgets/icons/Sketcher_Rotate.svg");
			translate->setIcon(":/widgets/icons/Sketcher_Translate.svg");
			symmetry->setIcon(":/widgets/icons/Sketcher_Symmetry.svg");
			fillet->setIcon(":/widgets/icons/Sketcher_CreateFillet.svg");
			offset->setIcon(":/widgets/icons/Sketcher_Offset.svg");
			toggleConstruction->setIcon(":/widgets/icons/Sketcher_ToggleConstruction.svg");
			external->setIcon(":/widgets/icons/Sketcher_Projection.svg");
			externalIntersection->setIcon(":/widgets/icons/Sketcher_Intersection.svg");
			self->setMovable(false);
			self->setFloatable(false);
			self->setIconSize(QSize(22, 22));

			// The sketch page, split by what the tool does: what is drawn, what edits
			// existing geometry, what brings geometry in from outside the sketch, and
			// the constraints. Two rows per group are what keeps the whole page within
			// a normal window: the constraints belong to the sketch theme, so they sit
			// beside the drawing tools instead of on a page of their own.
			addGroup("绘制", 6, { point->action(), line->action(), lineSet->action(),
				arc->action(), arcSlot->action(), ellipse->action(), bspline->action(),
				circle->action(), rectangle->action(), polygon->action(), slot->action() });
			addGroup("编辑", 3, { trimming->action(), rotate->action(), translate->action(),
				symmetry->action(), fillet->action(), offset->action() });
			addGroup("参考", 3, { toggleConstruction->action(),
				external->action(), externalIntersection->action() });
			// The constraints append two more groups to this toolbar.
			AddConstraintGroups(self, self);
			// A draw handler takes the clicks back, so it ends the external geometry
			// mode (the two would otherwise fight over the same button presses).
			const auto leaveExternalMode = [this]() {
				// Both flavours of the mode go down together with the handler that
				// takes the clicks over.
				for (AddExternalGeometryCommand* command
					: AddExternalGeometryCommand::commands()) {
					if (command->action()->isChecked()) {
						command->action()->setChecked(false);
					}
				}
				// And with the buttons goes the tool widget they had switched on.
				DisableExternalGeometryTool();
			};
			for (CreateCurveCommand* command : {
				point, line, lineSet, arc, arcSlot, ellipse, bspline, circle,
				rectangle, polygon, slot, trimming, rotate, translate, symmetry, fillet,
				offset
				}) {
				self->connect(
					command->action(), &QAction::triggered, self, leaveExternalMode);
			}
			retranslateUi();
		}
		RibbonGroup* addGroup(const QString& p_title, int p_columns,
			std::initializer_list<QAction*> p_actions) {
			auto* group = new RibbonGroup(p_title, self);
			for (auto* action : p_actions) {
				group->addAction(action);
			}
			group->setGrid(p_columns, 2);
			QAction* groupAction = self->addWidget(group);
			groupAction->setText(p_title);
			return group;
		}
		void setLabel(QAction* p_action, const QString& p_text, const QString& p_english) {
			p_action->setText(p_text);
			p_action->setToolTip(QStringLiteral("%1 (%2)").arg(p_text, p_english));
		}
		void retranslateUi() {
			setLabel(point->action(), QStringLiteral("点"), QStringLiteral("Point"));
			setLabel(line->action(), QStringLiteral("直线"), QStringLiteral("Line"));
			setLabel(lineSet->action(), QStringLiteral("折线"), QStringLiteral("LineSet"));
			setLabel(arc->action(), QStringLiteral("圆弧"), QStringLiteral("Arc"));
			setLabel(arcSlot->action(), QStringLiteral("圆弧槽"), QStringLiteral("ArcSlot"));
			setLabel(ellipse->action(), QStringLiteral("椭圆"), QStringLiteral("Ellipse"));
			setLabel(bspline->action(), QStringLiteral("样条"), QStringLiteral("Bspline"));
			setLabel(circle->action(), QStringLiteral("圆"), QStringLiteral("Circle"));
			setLabel(rectangle->action(), QStringLiteral("矩形"), QStringLiteral("Rectangle"));
			setLabel(polygon->action(), QStringLiteral("正多边形"), QStringLiteral("Polygon"));
			setLabel(slot->action(), QStringLiteral("槽"), QStringLiteral("Slot"));
			setLabel(trimming->action(), QStringLiteral("修剪"), QStringLiteral("Trimming"));
			setLabel(rotate->action(), QStringLiteral("旋转"), QStringLiteral("Rotate"));
			setLabel(translate->action(), QStringLiteral("平移"), QStringLiteral("Translate"));
			setLabel(symmetry->action(), QStringLiteral("对称"), QStringLiteral("Symmetry"));
			setLabel(fillet->action(), QStringLiteral("圆角"), QStringLiteral("Fillet"));
			setLabel(offset->action(), QStringLiteral("偏移"), QStringLiteral("Offset"));
			setLabel(toggleConstruction->action(), QStringLiteral("构造几何"),
				QStringLiteral("Toggle Construction Geometry"));
			setLabel(external->action(), QStringLiteral("外部几何"),
				QStringLiteral("External Geometry"));
			setLabel(externalIntersection->action(), QStringLiteral("外部相交"),
				QStringLiteral("External Intersection"));
		}
	private:
		friend class SketchToolbar;
		SketchToolbar* self = nullptr;
		CreateCurveCommand* point;
		CreateCurveCommand* line;
		CreateCurveCommand* lineSet;
		CreateCurveCommand* circle;
		CreateCurveCommand* rotate;
		CreateCurveCommand* translate;
		CreateCurveCommand* arc;
		CreateCurveCommand* arcSlot;
		CreateCurveCommand* ellipse;
		CreateCurveCommand* bspline;
		CreateCurveCommand* rectangle;
		CreateCurveCommand* polygon;
		CreateCurveCommand* slot;
		CreateCurveCommand* trimming;
		CreateCurveCommand* symmetry;
		CreateCurveCommand* fillet;
		CreateCurveCommand* offset;
		ToggleConstructionCommand* toggleConstruction = nullptr;
		AddExternalGeometryCommand* external = nullptr;
		AddExternalGeometryCommand* externalIntersection = nullptr;
		
	};

	SketchToolbar::SketchToolbar(const QString& title, QWidget* parent)
		:QToolBar(title, parent)
	{
		RegService(SketchToolbar,*this);
		constructor();
	}
	SketchToolbar::SketchToolbar(QWidget* parentObject) :QToolBar(parentObject)
	{
		RegService(SketchToolbar, *this);
		constructor();
	}
	SketchToolbar::~SketchToolbar()
	{
		if (mInternal) {
			delete mInternal;
			mInternal = nullptr;
		}
	}
	void SketchToolbar::disableAllHandlers()
	{
		auto& view = GetService(Editor::Panels::SceneView);

		auto& gizmoPass = view.GetRenderer().GetPass<Editor::Rendering::GizmoRenderPass>("ImRenderer");
	
		for (int i = 0; i < CreateCurveCommand::blackList.size(); i++) {
			
			gizmoPass.enableGizmoWidget(CreateCurveCommand::blackList[i], false);
			if (CreateCurveCommand::commandMap.find(CreateCurveCommand::blackList[i]) != CreateCurveCommand::commandMap.end()) {
				CreateCurveCommand::commandMap[CreateCurveCommand::blackList[i]]->action()->setChecked(false);
			}
		}
		uncheckExternalGeometry();
	}
	void SketchToolbar::uncheckExternalGeometry()
	{
		bool wasOn = false;
		for (AddExternalGeometryCommand* command : AddExternalGeometryCommand::commands()) {
			if (!command->action()->isChecked()) {
				continue;
			}
			// Keep the tool in sync with the buttons: this is also the way the mode is
			// left when the sketch stops being edited, or when Escape is pressed inside
			// the tool.
			command->action()->setChecked(false);
			wasOn = true;
		}
		if (wasOn) {
			DisableExternalGeometryTool();
		}
	}
	void SketchToolbar::setUncheckedAction(const std::string& name)
	{
		// Not every tool that lives in the gizmo pass is a drawing handler of this
		// toolbar (the smart dimension of the constraint toolbar is one of those), so
		// a name that is not in the map is simply nothing to uncheck - looking it up
		// with operator[] would put a null pointer in the map and crash on it.
		const auto it = CreateCurveCommand::commandMap.find(name);
		if (it != CreateCurveCommand::commandMap.end() && it->second != nullptr) {
			it->second->action()->setChecked(false);
		}
	}
	void SketchToolbar::constructor()
	{
		mInternal = new SketchToolbarInternal(this);
		mInternal->setup();
	}
}
