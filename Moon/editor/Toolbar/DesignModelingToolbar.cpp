#include "editor/Toolbar/DesignModelingToolbar.h"
#include "editor/Toolbar/RibbonGroup.h"
#include "editor/Command/command.h"
#include "editor/UI/TaskPanel/TaskViewWidget.h"
#include "editor/UI/TaskPanel/ExtrudeTaskDialog.h"
#include "editor/UI/TaskPanel/ThicknessTaskDialog.h"
#include "editor/UI/TaskPanel/FilletTask.h"
#include "editor/UI/TaskPanel/ChamferTask.h"
#include "editor/UI/TaskPanel/RevolutionTask.h"
#include "editor/UI/TaskPanel/DatumLineTask.h"
#include "editor/UI/TaskPanel/DatumPlaneTask.h"
#include "editor/UI/TaskPanel/PipeTask.h"
#include "editor/UI/TaskPanel/LoftTask.h"
#include "editor/UI/TaskPanel/HelixTask.h"
#include "editor/UI/TaskPanel/PolarPatternTask.h"
#include "editor/UI/TaskPanel/MirrorTask.h"
#include "editor/UI/TaskPanel/LinearPatternTask.h"
#include "TopoShape.h"
#include "Core/Global/ServiceLocator.h"
#include "core/ViewTool.h"
#include "renderer/SceneView.h"
#include "renderer/GizmoRenderPass.h"
#include <QCoreApplication>
#include <initializer_list>
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
		if (name == "DatumPlane") {
			return new DatumPlaneTask();
		}
		if (name == "Pipe") {
			return new PipeTask(nullptr, /*addSubType*/ 0);
		}
		if (name == "PipeCut") {
			return new PipeTask(nullptr, /*addSubType*/ 1);
		}
		if (name == "Loft") {
			return new LoftTask(nullptr, /*addSubType*/ 0);
		}
		if (name == "LoftCut") {
			return new LoftTask(nullptr, /*addSubType*/ 1);
		}
		if (name == "Helix") {
			return new HelixTask(nullptr, /*addSubType*/ 0);
		}
		if (name == "HelixCut") {
			return new HelixTask(nullptr, /*addSubType*/ 1);
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
			additivePipeCommand = new DesignModelCommand(self, "Pipe");
			additivePipeCommand->setIcon(":/widgets/icons/partdesign/PartDesign_AdditivePipe.svg");
			subtractivePipeCommand = new DesignModelCommand(self, "PipeCut");
			subtractivePipeCommand->setIcon(":/widgets/icons/partdesign/PartDesign_SubtractivePipe.svg");
			additiveLoftCommand = new DesignModelCommand(self, "Loft");
			additiveLoftCommand->setIcon(
				":/widgets/icons/partdesign/PartDesign_AdditiveLoft.svg");
			subtractiveLoftCommand = new DesignModelCommand(self, "LoftCut");
			subtractiveLoftCommand->setIcon(
				":/widgets/icons/partdesign/PartDesign_SubtractiveLoft.svg");
			additiveHelixCommand = new DesignModelCommand(self, "Helix");
			additiveHelixCommand->setIcon(
				":/widgets/icons/partdesign/PartDesign_AdditiveHelix.svg");
			subtractiveHelixCommand = new DesignModelCommand(self, "HelixCut");
			subtractiveHelixCommand->setIcon(
				":/widgets/icons/partdesign/PartDesign_SubtractiveHelix.svg");
			datumLineCommand = new DesignModelCommand(self, "DatumLine");
			datumLineCommand->setIcon(":/widgets/icons/partdesign/PartDesign_Line.svg");
			datumPlaneCommand = new DesignModelCommand(self, "DatumPlane");
			datumPlaneCommand->setIcon(":/widgets/icons/partdesign/PartDesign_Plane.svg");
			polarPatternCommand = new DesignModelCommand(self, "PolarPattern");
			polarPatternCommand->setIcon(
				":/widgets/icons/partdesign/PartDesign_PolarPattern.svg");
			mirrorCommand = new DesignModelCommand(self, "Mirror");
			mirrorCommand->setIcon(
				":/widgets/icons/partdesign/PartDesign_Mirrored.svg");
			linearPatternCommand = new DesignModelCommand(self, "LinearPattern");
			linearPatternCommand->setIcon(
				":/widgets/icons/partdesign/PartDesign_LinearPattern.svg");
			// The toolbar is a ribbon: one group per theme, in the order a part is
			// actually built in. The old flat row put all eighteen icons together,
			// where the only way to find one was to read its tooltip.
			self->setMovable(false);
			self->setFloatable(false);
			self->setIconSize(QSize(22, 22));

			RibbonGroup* lastGroup = nullptr;
			lastGroup = addGroup("基于轮廓", 2, { padCommand, pocketCommand, revolveCommand, grooveCommand });
			lastGroup = addGroup("扫掠", 3, { additivePipeCommand, subtractivePipeCommand,
				additiveLoftCommand, subtractiveLoftCommand,
				additiveHelixCommand, subtractiveHelixCommand });
			lastGroup = addGroup("修饰", 2, { filletCommand, chamferCommand, thicknessCommand });
			lastGroup = addGroup("基准", 2, { datumLineCommand, datumPlaneCommand });
			lastGroup = addGroup("变换", 2, { polarPatternCommand, linearPatternCommand, mirrorCommand });
			if (lastGroup != nullptr) {
				// Nothing follows the last group, so its right edge has no rule.
				lastGroup->setDrawSeparator(false);
			}

			retranslateUi();
		}
		RibbonGroup* addGroup(const QString& p_title, int p_columns,
			std::initializer_list<DesignModelCommand*> p_commands) {
			auto* group = new RibbonGroup(p_title, self);
			for (auto* command : p_commands) {
				group->addAction(command->action());
			}
			group->setGrid(p_columns, 2);
			// The action the toolbar gets for the widget is what its overflow menu
			// lists when the window is too narrow to show the whole ribbon.
			QAction* groupAction = self->addWidget(group);
			groupAction->setText(p_title);
			return group;
		}
		void setLabel(DesignModelCommand* p_command, const QString& p_text, const QString& p_english) {
			p_command->action()->setText(p_text);
			// The English name is what the feature goes by in the tree and in the task
			// panels, so the tooltip keeps both rather than the label alone.
			p_command->action()->setToolTip(QStringLiteral("%1 (%2)").arg(p_text, p_english));
		}
		void retranslateUi() {
			setLabel(padCommand, QStringLiteral("凸台"), QStringLiteral("Pad"));
			setLabel(pocketCommand, QStringLiteral("凹槽"), QStringLiteral("Pocket"));
			setLabel(revolveCommand, QStringLiteral("旋转"), QStringLiteral("Revolve"));
			setLabel(grooveCommand, QStringLiteral("开槽"), QStringLiteral("Groove"));
			setLabel(additivePipeCommand, QStringLiteral("加法管道"), QStringLiteral("Additive Pipe"));
			setLabel(subtractivePipeCommand, QStringLiteral("减法管道"), QStringLiteral("Subtractive Pipe"));
			setLabel(additiveLoftCommand, QStringLiteral("加法放样"), QStringLiteral("Additive Loft"));
			setLabel(subtractiveLoftCommand, QStringLiteral("减法放样"), QStringLiteral("Subtractive Loft"));
			setLabel(additiveHelixCommand, QStringLiteral("加法螺旋"), QStringLiteral("Additive Helix"));
			setLabel(subtractiveHelixCommand, QStringLiteral("减法螺旋"), QStringLiteral("Subtractive Helix"));
			setLabel(filletCommand, QStringLiteral("圆角"), QStringLiteral("Fillet"));
			setLabel(chamferCommand, QStringLiteral("倒角"), QStringLiteral("Chamfer"));
			setLabel(thicknessCommand, QStringLiteral("抽壳"), QStringLiteral("Thickness"));
			setLabel(datumLineCommand, QStringLiteral("基准线"), QStringLiteral("Datum Line"));
			setLabel(datumPlaneCommand, QStringLiteral("基准面"), QStringLiteral("Datum Plane"));
			setLabel(polarPatternCommand, QStringLiteral("环形阵列"), QStringLiteral("Polar Pattern"));
			setLabel(linearPatternCommand, QStringLiteral("线性阵列"), QStringLiteral("Linear Pattern"));
			setLabel(mirrorCommand, QStringLiteral("镜像"), QStringLiteral("Mirror"));
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
		DesignModelCommand* additivePipeCommand = nullptr;
		DesignModelCommand* subtractivePipeCommand = nullptr;
		DesignModelCommand* additiveLoftCommand = nullptr;
		DesignModelCommand* subtractiveLoftCommand = nullptr;
		DesignModelCommand* additiveHelixCommand = nullptr;
		DesignModelCommand* subtractiveHelixCommand = nullptr;
		DesignModelCommand* datumLineCommand = nullptr;
		DesignModelCommand* datumPlaneCommand = nullptr;
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
