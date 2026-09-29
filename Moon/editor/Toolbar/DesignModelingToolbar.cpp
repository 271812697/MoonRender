#include "editor/Toolbar/DesignModelingToolbar.h"
#include "editor/Command/command.h"
#include "editor/UI/TaskPanel/TaskViewWidget.h"
#include "editor/UI/TaskPanel/ExtrudeTaskDialog.h"
#include "editor/UI/TaskPanel/ThicknessTaskDialog.h"
#include "editor/UI/TaskPanel/FilletTask.h"
#include "editor/UI/TaskPanel/ChamferTask.h"
#include "editor/UI/TaskPanel/RevolutionTask.h"
#include "editor/UI/TaskPanel/DatumLineTask.h"
#include "editor/UI/TaskPanel/PolarPatternTask.h"
#include "editor/UI/TaskPanel/MirrorTask.h"
#include "editor/UI/TaskPanel/LinearPatternTask.h"
#include "TopoShape.h"
#include "Core/Global/ServiceLocator.h"
#include "core/ViewTool.h"
#include "renderer/SceneView.h"
#include "renderer/GizmoRenderPass.h"
#include <QCoreApplication>
namespace MOON {

	BaseTaskDialog* createTaskDialog(const std::string name) {
		if (name == "Pad") {
			ExtrudeTaskDialog* extrude= new ExtrudeTaskDialog(nullptr, ExtrudeType::Additive);
			return extrude;
		}
		if (name == "Thickness") {
			return new ThicknessTaskDialog();
		}
		if (name == "Fillet") {
			return new FilletTask();
		}
		if (name == "Chamfer") {
			return new ChamferTask();
		}
		if (name == "Pocket") {
			return new ExtrudeTaskDialog(nullptr, ExtrudeType::Subtractive);
		}
		if (name == "Revolve") {
			return new RevolutionTask(RevolutionType::ReAdditive,nullptr);
		}
		if (name == "Groove")
		{
			return new RevolutionTask(RevolutionType::ReSubtractive,nullptr);
		}
		if (name == "DatumLine") {
			return new DatumLineTask();
		}
		if (name == "PolarPattern") {
			return new PolarPatternTask();
		}
		if (name == "Mirror") {
			return new MirrorTask();
		}
		if (name == "LinearPattern") {
			return new LinearPatternTask();
		}
		return nullptr;
	}
	class DesignModelCommand : public Command
	{
	public:
		DesignModelCommand(QObject* parent,const std::string& name) :Command(parent),taskName(name) {
			auto action = new QAction(this);
			action->setCheckable(true);
			setAction(action);
		}
	protected:
		virtual void execute()override {
			auto& task = GetService(TaskViewWidget);
			if (!task.hasTask()) {
				task.setTaskDialog(createTaskDialog(taskName));
			}
		}
	private:
		std::string taskName = "";
	};
	class DesignModelingToolbar::DesignModelingToolbarInternal {
	public:

		DesignModelingToolbarInternal(DesignModelingToolbar* toolbar) :self(toolbar)
		{
			setup();
		}
		void setup() {
			padCommand = new DesignModelCommand(self, "Pad");
			padCommand->setIcon(":/widgets/icons/partdesign/PartDesign_Pad.svg");
			revolveCommand = new DesignModelCommand(self,"Revolve");
			revolveCommand->setIcon(":/widgets/icons/partdesign/PartDesign_Revolution.svg");
			thicknessCommand = new DesignModelCommand(self,"Thickness");
			thicknessCommand->setIcon(":/widgets/icons/partdesign/PartDesign_Thickness.svg");
			filletCommand = new DesignModelCommand(self,"Fillet");
			filletCommand->setIcon(":/widgets/icons/partdesign/PartDesign_Fillet.svg");
			chamferCommand = new DesignModelCommand(self, "Chamfer");
			chamferCommand->setIcon(":/widgets/icons/partdesign/PartDesign_Chamfer.svg");
			pocketCommand = new DesignModelCommand(self,"Pocket");
			pocketCommand->setIcon(":/widgets/icons/partdesign/PartDesign_Pocket.svg");
			grooveCommand = new DesignModelCommand(self,"Groove");
			grooveCommand->setIcon(":/widgets/icons/partdesign/PartDesign_Groove.svg");
			datumLineCommand = new DesignModelCommand(self, "DatumLine");
			datumLineCommand->setIcon(":/widgets/icons/partdesign/PartDesign_Line.svg");
			polarPatternCommand = new DesignModelCommand(self, "PolarPattern");
			polarPatternCommand->setIcon(
				":/widgets/icons/partdesign/PartDesign_PolarPattern.svg");
			mirrorCommand = new DesignModelCommand(self, "Mirror");
			mirrorCommand->setIcon(
				":/widgets/icons/partdesign/PartDesign_Mirrored.svg");
			linearPatternCommand = new DesignModelCommand(self, "LinearPattern");
			linearPatternCommand->setIcon(
				":/widgets/icons/partdesign/PartDesign_LinearPattern.svg");
			self->addAction(padCommand->action());			
			self->addAction(revolveCommand->action());
			self->addAction(thicknessCommand->action());
			self->addAction(filletCommand->action());
			self->addAction(chamferCommand->action());
			self->addAction(pocketCommand->action());
			self->addAction(grooveCommand->action());
			self->addAction(datumLineCommand->action());
			self->addAction(polarPatternCommand->action());
			self->addAction(mirrorCommand->action());
			self->addAction(linearPatternCommand->action());

			retranslateUi();
		}
		void retranslateUi() {
			padCommand->action()->setText(QCoreApplication::translate("DesignModelingToolbar", "Pad", nullptr));
			thicknessCommand->action()->setText(QCoreApplication::translate("DesignModelingToolbar", "Thickness", nullptr));
			filletCommand->action()->setText(QCoreApplication::translate("DesignModelingToolbar", "Fillet", nullptr));
			chamferCommand->action()->setText(QCoreApplication::translate("DesignModelingToolbar", "Chamfer", nullptr));
			pocketCommand->action()->setText(QCoreApplication::translate("DesignModelingToolbar", "Pocket", nullptr));
			revolveCommand->action()->setText(QCoreApplication::translate("DesignModelingToolbar", "Revolve", nullptr));
			grooveCommand->action()->setText(QCoreApplication::translate("DesignModelingToolbar", "Groove", nullptr));
			datumLineCommand->action()->setText(QCoreApplication::translate("DesignModelingToolbar", "Datum Line", nullptr));
			polarPatternCommand->action()->setText(QCoreApplication::translate("DesignModelingToolbar", "Polar Pattern", nullptr));
			mirrorCommand->action()->setText(QCoreApplication::translate("DesignModelingToolbar", "Mirror", nullptr));
			linearPatternCommand->action()->setText(QCoreApplication::translate("DesignModelingToolbar", "Linear Pattern", nullptr));
		}
	private:
		friend class DesignModelingToolbarToolbar;
		DesignModelingToolbar* self = nullptr;
		DesignModelCommand* padCommand = nullptr;
		DesignModelCommand* thicknessCommand = nullptr;
		DesignModelCommand* filletCommand = nullptr;
		DesignModelCommand* chamferCommand = nullptr;
		DesignModelCommand* pocketCommand = nullptr;
		DesignModelCommand* revolveCommand = nullptr;
		DesignModelCommand* grooveCommand = nullptr;
		DesignModelCommand* datumLineCommand = nullptr;
		DesignModelCommand* polarPatternCommand = nullptr;
		DesignModelCommand* mirrorCommand = nullptr;
		DesignModelCommand* linearPatternCommand = nullptr;
	};

	DesignModelingToolbar::DesignModelingToolbar(const QString& title, QWidget* parent)
		:QToolBar(title, parent)
	{
		constructor();
	}
	DesignModelingToolbar::DesignModelingToolbar(QWidget* parentObject) :QToolBar(parentObject)
	{
		constructor();
	}
	DesignModelingToolbar::~DesignModelingToolbar()
	{
		if (mInternal) {
			delete mInternal;
			mInternal = nullptr;
		}
	}
	void DesignModelingToolbar::constructor()
	{
		mInternal = new DesignModelingToolbarInternal(this);
	}
}
