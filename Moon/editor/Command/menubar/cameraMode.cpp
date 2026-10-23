#include "cameraMode.h"
#include "editor/Language.h"

#include "Core/Global/ServiceLocator.h"
#include "core/log.h"
#include "renderer/SceneView.h"

#include <QMenu>
#include <QtWidgets/QFileDialog>
#include <QCoreApplication>


namespace MOON {
	//-----------------------------------------------------------------------------
	CameraModeComand::CameraModeComand(QObject* parentObject)
		: Command(parentObject)
	{
		mActionOrtho = new QAction(this);
		MOON::Language::SetLabel(mActionOrtho, QStringLiteral("正交"), QStringLiteral("Orthographic"));
		mActionPersp = new QAction(this);
		MOON::Language::SetLabel(mActionPersp, QStringLiteral("透视"), QStringLiteral("Perspective"));
		mActionOrtho->setCheckable(true);
		mActionPersp->setCheckable(true);
		auto menu = new QMenu(static_cast<QWidget*>(parentObject));
		menu->addAction(mActionOrtho);
		menu->addAction(mActionPersp);
		auto group = new QActionGroup(menu);
		group->setExclusive(true);
		group->addAction(mActionOrtho);
		group->addAction(mActionPersp);

		auto action = new QAction(this);
		action->setText(Command::tr("Projection"));
		action->setMenu(menu);
		setAction(action);
		setIcon(":/widgets/icons/pqCamera.svg");
		
		action->setObjectName(QString::fromUtf8("actionCameraMode"));
		MOON::Language::SetLabel(action, QStringLiteral("相机模式"), QStringLiteral("&Camera Mode"));
		action->setStatusTip("Switch to Camera Mode");
		action->setShortcut(QCoreApplication::translate("pqFileMenuBuilder", "Ctrl+M", nullptr));

		QObject::connect(group, &QActionGroup::triggered, this, [=](const QAction* action) {
			auto& view=GetService(Editor::Panels::SceneView);
			if (action == mActionOrtho) {
				view.setCameraMode(Rendering::Settings::EProjectionMode::ORTHOGRAPHIC);
			}
			else
			{
				view.setCameraMode(Rendering::Settings::EProjectionMode::PERSPECTIVE);
			}
			});

	}

	void CameraModeComand::execute()
	{
		CORE_INFO("Switch to Camera Mode");
	}
}




