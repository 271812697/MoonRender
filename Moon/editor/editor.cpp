#include <qpushbutton.h>
#include <qboxlayout.h>
#include <QtWidgets/QApplication>
#include <QMenuBar>
#include <QSplitter>
#include <QtWidgets/QDockWidget>
#include <QStackedWidget>
#include <QStackedLayout>
#include "editor.h"
#include "UI/TreeViewPanel/hierarchypanel.h"
#include "UI/SettingPanel/SettingPanel.h"
#include "UI/PropertyPanel/PropertyPanel.h"
#include "UI/MaterialPanel/MaterialEditorPanel.h"
#include "MulViewPanel.h"
//#include "UI/ReousrcePanel/resourcePanel.h"
#include "UI/LogPanel/LogPanel.h"
#include "UI/ContentBrowser/ContentBrowserPanel.h"
#include "UI/TaskPanel/TaskViewPanel.h"
#include "editor/Toolbar/sketchToolbar.h"
#include "editor/Toolbar/ContraintToolbar.h"
#include "editor/Toolbar/primitiveToolbar.h"
#include "editor/Toolbar/DesignModelingToolbar.h"
#include "Command/menubar/openFile.h"
#include "Command/menubar/saveFile.h"
#include "Command/menubar/exportFile.h"
#include "Command/menubar/cameraMode.h"
#include "Command/menubar/fpsStat.h"
#include "Command/menubar/visibleview.h"
#include "Command/menubar/sketch.h"
#include "Command/menubar/bodyCommand.h"
#include "Command/menubar/projectCommand.h"
#include "UI/HomePanel/HomePanel.h"
#include "UI/HomePanel/RecentProjects.h"
#include "editor/View/sceneview/viewerwidget.h"
#include "core/Global/ServiceLocator.h"
#include "editor/UI/TreeViewPanel/treeViewpanel.h"
#include "editor/UI/TaskPanel/TaskViewWidget.h"

namespace MOON {
	class Editor::EditorInternal {
	public:
		EditorInternal(Editor* editor) :self(editor)
		{

		}
		void initPanels() {
			QIcon icon;
			icon.addFile(QString::fromUtf8(":/widgets/icons/awesomeface.png"), QSize(), QIcon::Normal, QIcon::Off);
			self->setWindowIcon(icon);
			// The window is two pages: the start page the application opens on (make or
			// open a project, and the ones that were open last) and the workspace - the
			// viewport with the panels around it. Choosing a project is what brings the
			// workspace up, and closing one puts the start page back.
			auto* pages = new QStackedWidget(self);
			self->setCentralWidget(pages);
			auto* workspace = new QWidget(pages);
			pages->addWidget(workspace);
			auto centralwidget_layout = new QHBoxLayout(workspace);
			auto middlePanel = new MulViewPanel(workspace);
			QSizePolicy sizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
			sizePolicy.setHeightForWidth(middlePanel->sizePolicy().hasHeightForWidth());
			middlePanel->setSizePolicy(sizePolicy);
			centralwidget_layout->addWidget(middlePanel);
			centralwidget_layout->setContentsMargins(0, 0, 0, 0);	
			mWorkspaceIndex = pages->indexOf(workspace);
			auto* home = new HomePanel(pages);
			pages->addWidget(home);
			home->AttachWorkspace(pages, mWorkspaceIndex);

			auto hierarchypanel = new Hierarchypanel(self);
			hierarchypanel->setAllowedAreas(Qt::AllDockWidgetAreas);
			hierarchypanel->setWindowTitle(QApplication::translate("Hierarchypanel", "Hierarchy", nullptr));
			
			auto settingPanel = new SettingPanel(self);
			settingPanel->setAllowedAreas(Qt::AllDockWidgetAreas);
			settingPanel->setWindowTitle(QApplication::translate("Settingpanel", "Setting", nullptr));
			
			auto propertyPanel=new PropertyPanel(self);
			propertyPanel->setAllowedAreas(Qt::AllDockWidgetAreas);
			propertyPanel->setWindowTitle(QApplication::translate("Propertypanel", "Property", nullptr));

			// The material editor: pick a shader, edit it, apply, and watch the preview - it
			// compiles the source in process (MoonMatcLib), so the pipeline is exercised end to
			// end without a rebuild.
			auto materialPanel = new MaterialEditorPanel(self);
			materialPanel->setWindowTitle(QApplication::translate("MaterialEditorPanel", "Material", nullptr));

			auto taskViewPanel = new TaskViewPanel(self);
			taskViewPanel->setAllowedAreas(Qt::AllDockWidgetAreas);
			taskViewPanel->setWindowTitle(QApplication::translate("TaskViewPanel", "Task View", nullptr));

			self->addDockWidget(Qt::LeftDockWidgetArea, hierarchypanel);
			self->addDockWidget(Qt::LeftDockWidgetArea, settingPanel);
			self->addDockWidget(Qt::RightDockWidgetArea,propertyPanel);
			self->addDockWidget(Qt::RightDockWidgetArea, taskViewPanel);

			// The material editor is a window of its own (see MaterialEditorPanel): the content
			// browser opens it on the material that was double-clicked, and it starts closed so
			// it does not cover the workspace on startup.
			materialPanel->hide();

			auto logPanelDock = new LogPanel(self);
			self->addDockWidget(Qt::BottomDockWidgetArea, logPanelDock);

			// The content browser sits in the same bottom area as the log: materials and
			// textures are workspace assets, so it belongs to the workspace pages rather than
			// to the tool windows. The View menu can still hide it.
			auto contentBrowser = new ContentBrowserPanel(self);
			contentBrowser->setAllowedAreas(Qt::AllDockWidgetAreas);
			self->addDockWidget(Qt::BottomDockWidgetArea, contentBrowser);
			self->tabifyDockWidget(logPanelDock, contentBrowser);
			contentBrowser->raise();
			//auto debugWidget = new DebugWidget(self);
			//self->addDockWidget(Qt::LeftDockWidgetArea, debugWidget);
			//self->tabifyDockWidget(HierarchypanelDock, debugWidget);

			// The panels that belong to the workspace. The material editor is not one of them: it
			// is a tool toggled from the View menu, so it keeps its state across pages.
			mDocks = { hierarchypanel, settingPanel, propertyPanel, taskViewPanel, logPanelDock,
				contentBrowser };
			QObject::connect(
				pages, &QStackedWidget::currentChanged, self, [this](int p_index) {
					// The panels are part of the workspace: there is no document to show
					// in them while the start page is up, so they go away with it.
					setDocksVisible(p_index == mWorkspaceIndex);
				});
			setDocksVisible(false);

			connectSignals();
		}
		/** Shows or hides the panels around the viewport - see the stack's
		 * currentChanged. */
		void setDocksVisible(bool p_visible)
		{
			for (QDockWidget* dock : mDocks) {
				if (dock != nullptr) {
					dock->setVisible(p_visible);
				}
			}
		}
		void showStartPage()
		{
			::Core::Global::ServiceLocator::Get<HomePanel>().ShowStartPage();
		}
		void connectSignals() {
			//select features
			auto& tree = ::Core::Global::ServiceLocator::Get<::MOON::TreeViewPanel>();
			auto& task = ::Core::Global::ServiceLocator::Get<::MOON::TaskViewWidget>();
			connect(&tree, &TreeViewPanel::selectFeature, &task, &TaskViewWidget::onSelectedFeature);
		
		}
		void buildFileMenu() {
			auto newProjectCommand = new NewProjectCommand(self);
			auto closeProjectCommand = new CloseProjectCommand(self);
			auto openFileCommand=new OpenFileCommand(self);
			auto saveFileCommand = new SaveFileCommand(self, false);
			auto saveAsFileCommand = new SaveFileCommand(self, true);
			auto exportFileCommand = new ExportFileCommand(self);
			auto newBodyCommand = new NewBodyCommand(self);
			
			menu_File->addAction(newProjectCommand->action());
			menu_File->addAction(openFileCommand->action());
			menu_File->addAction(saveFileCommand->action());
			menu_File->addAction(saveAsFileCommand->action());
			menu_File->addAction(exportFileCommand->action());
			menu_File->addAction(closeProjectCommand->action());
			menu_File->addSeparator();
			menu_File->addAction(newBodyCommand->action());

			// The start page asks for the two things it offers by signal, and they are
			// triggered here: opening a project means the same wherever it is asked
			// for, and the commands are the ones that know how.
			auto& home = ::Core::Global::ServiceLocator::Get<HomePanel>();
			connect(
				&home, &HomePanel::newProjectRequested,
				newProjectCommand->action(), &QAction::trigger);
			connect(
				&home, &HomePanel::openProjectRequested,
				openFileCommand->action(), &QAction::trigger);
			connect(&home, &HomePanel::projectRequested, self, [](const QString& p_path) {
				// The same road the Open command takes, once it has a file.
				GetViewerWidget.onReadFile(p_path);
				});
			// A document that came up (opened from the page, the menu, or a drop) is one
			// of the recent projects from then on.
			connect(
				&GetViewerWidget, &ViewerWidget::documentOpened, self,
				[](const QString& p_path) {
					if (!p_path.isEmpty()) {
						RecentProjects::Remember(p_path);
					}
				});
		}
		void buildDisplayMenu() {
			auto cameraModeCommand = new CameraModeComand(self);
			menu_Display->addAction(cameraModeCommand->action());
			auto fpsStatCommand = new FpsStatCommand(self);
			menu_Display->addAction(fpsStatCommand->action());
		}
		void buildViewMenu() {
			auto visible=new VisibleViewCommand(menu_View);
			visible->setUp(menu_View);
		}
		void buildSketchMenu() {
			auto sketchCommand = new SketchCommand(self);
			menu_sketch->addAction(sketchCommand->action());
		}
		void buildMenu() {
			mMenubar = new QMenuBar(self);
			mMenubar->setObjectName(QString::fromUtf8("menubar"));
			//mMenubar->setGeometry(QRect(0, 0, 1152, 20));

			self->setMenuBar(mMenubar);
			menu_File = new QMenu(mMenubar);
			menu_Display = new QMenu(mMenubar);
			menu_View = new QMenu(mMenubar);
			menu_sketch = new QMenu(mMenubar);
			mMenubar->addAction(menu_File->menuAction());
			mMenubar->addAction(menu_Display->menuAction());
			mMenubar->addAction(menu_View->menuAction());
			mMenubar->addAction(menu_sketch->menuAction());
			buildFileMenu();
			buildDisplayMenu();
			buildViewMenu();
			buildSketchMenu();
	
		}
		void buildToolBar() {
			QToolBar* sketchtoolBar = new SketchToolbar(self);
			sketchtoolBar->layout()->setSpacing(0);
			self->addToolBar(Qt::TopToolBarArea, sketchtoolBar);
			QToolBar* sketchContraintToolBar = new ConstraintToolbar(self);
			sketchContraintToolBar->layout()->setSpacing(0);
			self->addToolBar(Qt::TopToolBarArea, sketchContraintToolBar);
			QToolBar* primitiveToolBar = new PrimitiveToolbar(self);
			primitiveToolBar->layout()->setSpacing(0);
			self->addToolBar(Qt::TopToolBarArea, primitiveToolBar);
			DesignModelingToolbar* designModelingToolbar = new DesignModelingToolbar(self);
			designModelingToolbar->layout()->setSpacing(0);
			self->addToolBar(Qt::TopToolBarArea, designModelingToolbar);

		}
		void retranslateUi() {
			
			self->setWindowTitle(QCoreApplication::translate("Editor", "MainWindow", nullptr));
			menu_File->setTitle("&File");
			menu_Display->setTitle("&Display");
			menu_View->setTitle("&View");
			menu_sketch->setTitle("&Sketch");
		
		}
	private:
		Editor* self = nullptr;
		QSplitter* vert_splitter_ = nullptr;
		QMenuBar* mMenubar;
		QMenu* menu_File;
		QMenu* menu_Display;
		QMenu* menu_View;
		QMenu* menu_sketch;
		/** Which page of the central stack is the workspace; the other page is the
		 * start page. */
		int mWorkspaceIndex = 0;
		/** The panels around the viewport. They are hidden while the start page is up -
		 * see the stack's currentChanged in initPanels. */
		std::vector<QDockWidget*> mDocks;
	};
	Editor::Editor() :mInternal(new EditorInternal(this))
	{
		mInternal->initPanels();
		mInternal->buildMenu();
		mInternal->buildToolBar();
		mInternal->retranslateUi();
		// The application opens on the start page: opening or making a project is what
		// brings the workspace up.
		mInternal->showStartPage();
	}
	Editor::~Editor()
	{
		delete mInternal;
	}
}
