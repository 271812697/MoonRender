#include "editor/Toolbar/sketchToolbar.h"
#include "editor/Command/command.h"
#include "Core/Global/ServiceLocator.h"
#include "renderer/SceneView.h"
#include "renderer/GizmoRenderPass.h"
#include "Sketcher/SketcherObjManager.h"
#include "Sketcher/SketcherObj.h"
#include "core/log.h"
#include <QCoreApplication>
namespace MOON {

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

	/** Turns the add-external-geometry mode of the active sketch on and off.
	 *
	 * This is not a draw handler: the mode lives on the sketch, which resolves the
	 * clicked sub-shape of another feature into a curve projected into its plane
	 * (see SketcherObj::addExternalGeometry). It is still exclusive with the
	 * handlers, because both of them want the same clicks.
	 *
	 * The command comes in two flavours that share the one mode: the reference is
	 * either the projection of the picked shape or the section of it with the sketch
	 * plane. They therefore switch each other off. */
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
			SketcherObj* sketch
				= SketcherObjManager::instance().GetCurrentActiveSketcherObj();
			if (sketch == nullptr) {
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
				auto& view = GetService(Editor::Panels::SceneView);
				auto& gizmoPass
					= view.GetRenderer().GetPass<Editor::Rendering::GizmoRenderPass>("ImRenderer");
				for (int i = 0; i < static_cast<int>(CreateCurveCommand::blackList.size()); i++) {
					gizmoPass.enableGizmoWidget(CreateCurveCommand::blackList[i], false);
					if (CreateCurveCommand::commandMap.find(CreateCurveCommand::blackList[i])
						!= CreateCurveCommand::commandMap.end()) {
						CreateCurveCommand::commandMap[CreateCurveCommand::blackList[i]]
							->action()->setChecked(false);
					}
				}
			}
			sketch->setExternalGeometryIntersection(m_intersection);
			sketch->setExternalGeometryMode(value);
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
						if (SketcherObj* sketch
							= SketcherObjManager::instance().GetCurrentActiveSketcherObj()) {
							sketch->setExternalGeometryMode(false);
						}
					}
				}
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
		for (AddExternalGeometryCommand* command : AddExternalGeometryCommand::commands()) {
			if (!command->action()->isChecked()) {
				continue;
			}
			// Keep the sketch in sync with the buttons: this is also the way the mode is
			// left when the sketch stops being edited.
			command->action()->setChecked(false);
			if (SketcherObj* sketch
				= SketcherObjManager::instance().GetCurrentActiveSketcherObj()) {
				sketch->setExternalGeometryMode(false);
			}
		}
	}
	void SketchToolbar::setUncheckedAction(const std::string& name)
	{
		CreateCurveCommand::commandMap[name]->action()->setChecked(false);
	}
	void SketchToolbar::constructor()
	{
		mInternal = new SketchToolbarInternal(this);
		mInternal->setup();
	}
}
