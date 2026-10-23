#include "visibleview.h"
#include "editor/Language.h"
#include "editor/UI/TreeViewPanel/hierarchypanel.h"
#include "editor/UI/SettingPanel/SettingPanel.h"
#include "editor/UI/PropertyPanel/PropertyPanel.h"
#include "editor/UI/TaskPanel/TaskViewPanel.h"
#include "editor/UI/MaterialPanel/MaterialEditorPanel.h"
#include "editor/UI/LogPanel/LogPanel.h"
#include "editor/UI/ContentBrowser/ContentBrowserPanel.h"
#include "Core/Global/ServiceLocator.h"
#include "core/log.h"
#include "renderer/SceneView.h"

#include <QMenu>
#include <QtWidgets/QFileDialog>
#include <QCoreApplication>


namespace MOON {
	VisibleViewCommand::VisibleViewCommand(QObject* parent):QObject(parent)
	{
		
	}
	void VisibleViewCommand::setUp(QMenu* menu)
	{
		auto hierarchypanel = new QAction(this);
		auto settingPanel = new QAction(this);
		auto propertyPanel = new QAction(this);
		auto taskViewPanel = new QAction(this);
		auto logPanel = new QAction(this);
		MOON::Language::SetLabel(hierarchypanel, QStringLiteral("层级"), QStringLiteral("Hierarchy"));
		MOON::Language::SetLabel(settingPanel, QStringLiteral("设置"), QStringLiteral("Setting"));
		MOON::Language::SetLabel(propertyPanel, QStringLiteral("属性"), QStringLiteral("Property"));
		MOON::Language::SetLabel(taskViewPanel, QStringLiteral("任务视图"), QStringLiteral("Task View"));
		MOON::Language::SetLabel(logPanel, QStringLiteral("日志"), QStringLiteral("Log"));
		hierarchypanel->setCheckable(true);
		hierarchypanel->setChecked(true);
		settingPanel->setCheckable(true);
		settingPanel->setChecked(true);
		propertyPanel->setCheckable(true);
		propertyPanel->setChecked(true);
		taskViewPanel->setCheckable(true);
		taskViewPanel->setChecked(true);
		logPanel->setCheckable(true);
		logPanel->setChecked(true);
		menu->addAction(hierarchypanel);
		menu->addAction(settingPanel);
		menu->addAction(propertyPanel);
		menu->addAction(taskViewPanel);

		// The material editor is a window of its own; this action opens it (the content browser
		// opens it too, on the material that was double-clicked).
		auto* materialEditor = new QAction(this);
		MOON::Language::SetLabel(materialEditor, QStringLiteral("材质编辑器"), QStringLiteral("Material Editor"));
		menu->addAction(materialEditor);
		connect(materialEditor, &QAction::triggered, [] {
			auto& panel = GetService(MOON::MaterialEditorPanel);
			panel.show();
			panel.raise();
			panel.activateWindow();
		});

		// The content browser is a workspace panel, but its action is still the dock's own
		// toggle so the check mark follows the title bar's close button.
		auto* contentBrowser = GetService(MOON::ContentBrowserPanel).toggleViewAction();
		
		menu->addAction(contentBrowser);

		menu->addAction(logPanel);

		connect(hierarchypanel, &QAction::triggered, [](bool check) {
			if (check) {
				GetService(MOON::Hierarchypanel).show();
			}
			else
			{
				GetService(MOON::Hierarchypanel).hide();
			}

			});
		connect(settingPanel, &QAction::triggered, [](bool check) {
			if (check) {
				GetService(MOON::SettingPanel).show();
			}
			else
			{
				GetService(MOON::SettingPanel).hide();
			}

			});
		connect(propertyPanel, &QAction::triggered, [](bool check) {
			if (check) {
				GetService(MOON::PropertyPanel).show();
			}
			else
			{
				GetService(MOON::PropertyPanel).hide();
			}

			});
		connect(taskViewPanel, &QAction::triggered, [](bool check) {
			if (check) {
				GetService(MOON::TaskViewPanel).show();
			}
			else
			{
				GetService(MOON::TaskViewPanel).hide();
			}

			});
		connect(logPanel, &QAction::triggered, [](bool check) {
			if (check) {
				GetService(MOON::LogPanel).show();
			}
			else
			{
				GetService(MOON::LogPanel).hide();
			}

			});

	}
}




