#include "editor/Toolbar/primitiveToolbar.h"
#include "editor/Toolbar/RibbonGroup.h"
#include "editor/Command/command.h"
#include "Core/Global/ServiceLocator.h"
#include "renderer/SceneView.h"
#include "renderer/GizmoRenderPass.h"
#include <QCoreApplication>
#include <initializer_list>
namespace MOON {

	class  CreatePrimitiveCommand : public Command
	{
	public:
		CreatePrimitiveCommand(QObject* parent,const std::string& handler) :Command(parent),handlerName(handler) {
			auto action = new QAction(this);
			action->setCheckable(true);
			setAction(action);
		}
	protected:
		virtual void execute()override {
			bool value = action()->isChecked();
			auto& view = GetService(Editor::Panels::SceneView);
			auto& gizmoPass = view.GetRenderer().GetPass<Editor::Rendering::GizmoRenderPass>("ImRenderer");
			gizmoPass.enableGizmoWidget(handlerName, value);
			//if (value) {
			//	for (int i = 0;i < blackList.size();i++) {
			//		if (blackList[i] != handlerName) {
			//			gizmoPass.enableGizmoWidget(blackList[i], false);
			//			if (commandMap.find(blackList[i]) != commandMap.end()) {
			//				commandMap[blackList[i]]->action()->setChecked(false);
			//			}
			//		}
			//	}
			//}
		}
	private:
		std::string handlerName = "";
			 
	};
	
	class PrimitiveToolbar::PrimitiveToolbarInternal {
	public:

		PrimitiveToolbarInternal(PrimitiveToolbar* toolbar) :self(toolbar)
		{
		}
		void setup() {
			box = new CreatePrimitiveCommand(self, "PrimitiveBox");
			box->setIcon(":/widgets/icons/model32/Part_Box_Parametric.svg");
			cone = new CreatePrimitiveCommand(self, "PrimitiveCone");
			cone->setIcon(":/widgets/icons/model32/Part_Cone_Parametric.svg");
			cylinder = new CreatePrimitiveCommand(self, "PrimitiveCylinder");
			cylinder->setIcon(":/widgets/icons/model32/Part_Cylinder_Parametric.svg");
			sphere = new CreatePrimitiveCommand(self, "PrimitiveSphere");
			sphere->setIcon(":/widgets/icons/model32/Part_Sphere_Parametric.svg");

			self->setMovable(false);
			self->setFloatable(false);
			self->setIconSize(QSize(22, 22));
			auto* group = new RibbonGroup(QStringLiteral("基本体"), self);
			for (auto* action : { box->action(), sphere->action(), cylinder->action(), cone->action() }) {
				group->addAction(action);
			}
			group->setGrid(2, 2);
			group->setDrawSeparator(false);
			QAction* groupAction = self->addWidget(group);
			groupAction->setText(QStringLiteral("基本体"));

			retranslateUi();
		}
		void setLabel(QAction* p_action, const QString& p_text, const QString& p_english) {
			p_action->setText(p_text);
			p_action->setToolTip(QStringLiteral("%1 (%2)").arg(p_text, p_english));
		}
		void retranslateUi() {
			setLabel(box->action(), QStringLiteral("立方体"), QStringLiteral("Box"));
			setLabel(sphere->action(), QStringLiteral("球体"), QStringLiteral("Sphere"));
			setLabel(cylinder->action(), QStringLiteral("圆柱"), QStringLiteral("Cylinder"));
			setLabel(cone->action(), QStringLiteral("圆锥"), QStringLiteral("Cone"));
		}
	private:
		friend class PrimitiveToolbar;
		PrimitiveToolbar* self = nullptr;
		CreatePrimitiveCommand* box;
		CreatePrimitiveCommand* cone;
		CreatePrimitiveCommand* cylinder;
		CreatePrimitiveCommand* sphere;

	};

	PrimitiveToolbar::PrimitiveToolbar(const QString& title, QWidget* parent)
		:QToolBar(title, parent)
	{
		constructor();
	}
	PrimitiveToolbar::PrimitiveToolbar(QWidget* parentObject) :QToolBar(parentObject)
	{
		constructor();
	}
	PrimitiveToolbar::~PrimitiveToolbar()
	{
		if (mInternal) {
			delete mInternal;
			mInternal = nullptr;
		}
	}
	void PrimitiveToolbar::constructor()
	{
		mInternal = new PrimitiveToolbarInternal(this);
		mInternal->setup();
	}
}