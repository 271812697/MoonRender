#include "editor/Toolbar/sketchToolbar.h"
#include "editor/Command/command.h"
#include "Core/Global/ServiceLocator.h"
#include "renderer/SceneView.h"
#include "renderer/GizmoRenderPass.h"
#include "Sketcher/SketcherObjManager.h"
#include "Sketcher/SketcherObj.h"
#include "Interactive/Widgets/DrawSketchHandlerExternalGeometry.h"
#include "core/log.h"
#include <QCoreApplication>
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
			symmetry=new CreateCurveCommand(self, "DrawSketchHandlerSymmetry");
			fillet = new CreateCurveCommand(self, "DrawSketchHandlerFillet");
			offset = new CreateCurveCommand(self, "DrawSketchHandlerOffset");
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
			symmetry->setIcon(":/widgets/icons/Sketcher_Symmetry.svg");
			fillet->setIcon(":/widgets/icons/Sketcher_CreateFillet.svg");
			offset->setIcon(":/widgets/icons/Sketcher_Offset.svg");
			external->setIcon(":/widgets/icons/Sketcher_Projection.svg");
			externalIntersection->setIcon(":/widgets/icons/Sketcher_Intersection.svg");
			self->addAction(point->action());
			self->addAction(line->action());
			self->addAction(lineSet->action());
			self->addAction(arc->action());
			self->addAction(arcSlot->action());
			self->addAction(ellipse->action());
			self->addAction(bspline->action());
			self->addAction(circle->action());
			self->addAction(rectangle->action());
			self->addAction(polygon->action());
			self->addAction(slot->action());
			self->addAction(trimming->action());
			self->addAction(rotate->action());
			self->addAction(symmetry->action());
			self->addAction(fillet->action());
			self->addAction(offset->action());
			self->addAction(external->action());
			self->addAction(externalIntersection->action());
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
				rectangle, polygon, slot, trimming, rotate, symmetry, fillet, offset
				}) {
				self->connect(
					command->action(), &QAction::triggered, self, leaveExternalMode);
			}
			retranslateUi();
		}
		void retranslateUi() {
			point->action()->setText(QCoreApplication::translate("SketchToolbar", "Point", nullptr));
			line->action()->setText(QCoreApplication::translate("SketchToolbar", "Line", nullptr));
			lineSet->action()->setText(QCoreApplication::translate("SketchToolbar", "LineSet", nullptr));
			circle->action()->setText(QCoreApplication::translate("SketchToolbar", "Circle", nullptr));
			arc->action()->setText(QCoreApplication::translate("SketchToolbar", "Arc", nullptr));
			arcSlot->action()->setText(QCoreApplication::translate("SketchToolbar", "ArcSlot", nullptr));
			ellipse->action()->setText(QCoreApplication::translate("SketchToolbar", "Ellipse", nullptr));
			bspline->action()->setText(QCoreApplication::translate("SketchToolbar", "Bspline", nullptr));
			rectangle->action()->setText(QCoreApplication::translate("SketchToolbar", "Rectangle", nullptr));
			polygon->action()->setText(QCoreApplication::translate("SketchToolbar", "Polygon", nullptr));
			slot->action()->setText(QCoreApplication::translate("SketchToolbar", "Slot", nullptr));
			trimming->action()->setText(QCoreApplication::translate("SketchToolbar", "Trimming", nullptr));
			rotate->action()->setText(QCoreApplication::translate("SketchToolbar", "Rotate", nullptr));
			symmetry->action()->setText(QCoreApplication::translate("SketchToolbar", "Symmetry", nullptr));
			fillet->action()->setText(QCoreApplication::translate("SketchToolbar", "Fillet", nullptr));
			offset->action()->setText(QCoreApplication::translate("SketchToolbar", "Offset", nullptr));
			external->action()->setText(QCoreApplication::translate("SketchToolbar", "External Geometry", nullptr));
			externalIntersection->action()->setText(QCoreApplication::translate("SketchToolbar", "External Intersection", nullptr));
		}
	private:
		friend class SketchToolbar;
		SketchToolbar* self = nullptr;
		CreateCurveCommand* point;
		CreateCurveCommand* line;
		CreateCurveCommand* lineSet;
		CreateCurveCommand* circle;
		CreateCurveCommand* rotate;
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
