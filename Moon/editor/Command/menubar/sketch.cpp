#include "TopoShape.h"
#include "editor/Language.h"
#include <gp_Pln.hxx>
#include "editor/Command/menubar/sketch.h"
#include "editor/View/sceneview/viewerwidget.h"
#include "editor/UI/TaskPanel/TaskViewWidget.h"
#include "editor/UI/TaskPanel/SketchTaskDialog.h"
#include "renderer/SceneView.h"
#include "renderer/GizmoRenderPass.h"
#include "Core/Global/ServiceLocator.h"
#include "core/log.h"
#include "core/SelectionManager.h"
#include "core/component/CTopoShape.h"
#include "Sketcher/SketcherObjManager.h"
#include "Sketcher/SketcherObj.h"
#include <QtWidgets/QFileDialog>
#include <QCoreApplication>
namespace MOON {
	//-----------------------------------------------------------------------------
	SketchCommand::SketchCommand(QObject* parentObject)
		: Command(parentObject)
	{
		auto sketch=new QAction(this);
		sketch->setCheckable(true);
		setAction(sketch);
		sketch->setObjectName(QString::fromUtf8("createsketch"));
		MOON::Language::SetLabel(sketch, QStringLiteral("\u521b\u5efa\u8349\u56fe"), QStringLiteral("&Create Sketch"));
		sketch->setStatusTip("create sketch");
		//sketch->setShortcut(QCoreApplication::translate("pqFileMenuBuilder", "Ctrl+O", nullptr));
		QIcon icon9;
		icon9.addFile(QString::fromUtf8(":/widgets/icons/Sketcher_NewSketch.svg"), QSize(), QIcon::Normal, QIcon::Off);
		sketch->setIcon(icon9);
	}

	void SketchCommand::execute()
	{
		auto& task = GetService(TaskViewWidget);
		if (!task.hasTask()) {
			SketchTaskDialog* sketchTask=new SketchTaskDialog();
			task.setTaskDialog(sketchTask);
		}
	}
}




