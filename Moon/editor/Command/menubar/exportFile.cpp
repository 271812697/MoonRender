#include "editor/Command/menubar/exportFile.h"
#include "editor/View/sceneview/viewerwidget.h"
#include "renderer/SceneView.h"
#include "Core/Global/ServiceLocator.h"
#include "core/component/CTopoShape.h"
#include "TopoShape.h"
#include "core/log.h"
#include <QtWidgets/QFileDialog>
#include <QCoreApplication>
namespace MOON {
	//-----------------------------------------------------------------------------
	ExportFileCommand::ExportFileCommand(QObject* parentObject)
		: Command(parentObject)
	{
		auto openfile=new QAction(this);
		setAction(openfile);
		openfile->setObjectName(QString::fromUtf8("actionFileOpen"));
		openfile->setText("&Export");
		openfile->setStatusTip("Export");
		// Ctrl+S belongs to Save (see SaveFileCommand); export is a different action.
		openfile->setShortcut(QCoreApplication::translate("pqFileMenuBuilder", "Ctrl+E", nullptr));
		QIcon icon9;
		icon9.addFile(QString::fromUtf8(":/widgets/icons/pqOpen.svg"), QSize(), QIcon::Normal, QIcon::Off);
		openfile->setIcon(icon9);
	}

	void ExportFileCommand::execute()
	{
		QString selectedFilter;
		QString fileName = QFileDialog::getSaveFileName(nullptr,
			tr("Export File"),
			QDir::homePath(),
			tr("STL File (*.stl);;STEP File (*.step)"), // 格式分开写
			&selectedFilter);
		if (fileName.isEmpty())
			return;
		auto& viewer = GetService(Editor::Panels::SceneView);
		if (!viewer.IsSelectActor())
			return;

		auto& selectActor = *viewer.GetSelectedActor();
		if (!selectActor.HasComponent("CTopoShape"))
			return;

		auto& topoShape = selectActor.GetComponent<Core::ECS::Components::CTopoShape>()->GetTopoShape();

		// ======================
		// 关键：自动识别后缀
		// ======================
		QString ext = QFileInfo(fileName).suffix().toLower();
		if (ext.isEmpty()) {
			// 用户没写后缀 → 根据选择的过滤器自动补全
			if (selectedFilter.contains("*.stl"))
				fileName += ".stl";
			else if (selectedFilter.contains("*.step"))
				fileName += ".step";
		}

		// 重新获取后缀
		ext = QFileInfo(fileName).suffix().toLower();
		// ======================
		// 根据后缀导出不同格式
		// ======================
		if (ext == "stl") {
			topoShape.exportStl(fileName.toStdString().c_str(), true);
		}
		else if (ext == "step" || ext == "stp") {
			topoShape.exportStep(fileName.toStdString().c_str()); // 你必须有这个接口
		}
		CORE_INFO("Exported file {0}", fileName.toStdString());
	}
}




