#include <qpushbutton.h>
#include <qboxlayout.h>
#include <QtWidgets/QApplication>
#include <QMenuBar>
#include <QSplitter>
#include <QtWidgets/QDockWidget>
#include <QStackedWidget>
#include <QStackedLayout>
#include <QTabBar>
#include <QVariantList>
#include "editor.h"
#include "editor/Language.h"
#include "editor/Theme.h"
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
			MOON::Language::SetWindowTitle(hierarchypanel, QStringLiteral("层级"), QStringLiteral("Hierarchy"));
			
			auto settingPanel = new SettingPanel(self);
			settingPanel->setAllowedAreas(Qt::AllDockWidgetAreas);
			MOON::Language::SetWindowTitle(settingPanel, QStringLiteral("设置"), QStringLiteral("Setting"));
			
			auto propertyPanel=new PropertyPanel(self);
			propertyPanel->setAllowedAreas(Qt::AllDockWidgetAreas);
			MOON::Language::SetWindowTitle(propertyPanel, QStringLiteral("属性"), QStringLiteral("Property"));

			// The material editor: pick a shader, edit it, apply, and watch the preview - it
			// compiles the source in process (MoonMatcLib), so the pipeline is exercised end to
			// end without a rebuild.
			auto materialPanel = new MaterialEditorPanel(self);
			MOON::Language::SetWindowTitle(materialPanel, QStringLiteral("材质编辑器"), QStringLiteral("Material Editor"));

			auto taskViewPanel = new TaskViewPanel(self);
			taskViewPanel->setAllowedAreas(Qt::AllDockWidgetAreas);
			MOON::Language::SetWindowTitle(taskViewPanel, QStringLiteral("任务视图"), QStringLiteral("Task View"));

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

			// Colour scheme. Both entries re-apply the palette, the stylesheet and the icons in
			// place, so the switch is immediate; the choice is remembered in QSettings.
			QMenu* themeMenu = menu_View->addMenu(QStringLiteral("主题"));
			MOON::Language::SetLabel(themeMenu->menuAction(), QStringLiteral("主题"), QStringLiteral("Theme"));
			auto* darkAction = themeMenu->addAction(QStringLiteral("深色"));
			auto* lightAction = themeMenu->addAction(QStringLiteral("浅色"));
			MOON::Language::SetLabel(darkAction, QStringLiteral("深色"), QStringLiteral("Dark"));
			MOON::Language::SetLabel(lightAction, QStringLiteral("浅色"), QStringLiteral("Light"));
			darkAction->setCheckable(true);
			lightAction->setCheckable(true);
			darkAction->setChecked(MOON::Theme::Current() == MOON::Theme::ETheme::Dark);
			lightAction->setChecked(MOON::Theme::Current() == MOON::Theme::ETheme::Light);
			const auto switchTheme = [darkAction, lightAction](MOON::Theme::ETheme p_theme) {
				MOON::Theme::Set(p_theme);
				MOON::Theme::Apply(qApp);
				darkAction->setChecked(p_theme == MOON::Theme::ETheme::Dark);
				lightAction->setChecked(p_theme == MOON::Theme::ETheme::Light);
			};
			QObject::connect(darkAction, &QAction::triggered, self,
				[switchTheme] { switchTheme(MOON::Theme::ETheme::Dark); });
			QObject::connect(lightAction, &QAction::triggered, self,
				[switchTheme] { switchTheme(MOON::Theme::ETheme::Light); });

			// Language. Every caption and button remembered its two spellings, so the switch
			// rewrites the whole ribbon without rebuilding it.
			QMenu* languageMenu = menu_View->addMenu(QStringLiteral("语言"));
			MOON::Language::SetLabel(languageMenu->menuAction(), QStringLiteral("语言"), QStringLiteral("Language"));
			auto* chineseAction = languageMenu->addAction(QStringLiteral("中文"));
			auto* englishAction = languageMenu->addAction(QStringLiteral("英文"));
			MOON::Language::SetLabel(chineseAction, QStringLiteral("中文"), QStringLiteral("Chinese"));
			MOON::Language::SetLabel(englishAction, QStringLiteral("英文"), QStringLiteral("English"));
			chineseAction->setCheckable(true);
			englishAction->setCheckable(true);
			chineseAction->setChecked(MOON::Language::Current() == MOON::Language::ELanguage::Chinese);
			englishAction->setChecked(MOON::Language::Current() == MOON::Language::ELanguage::English);
			const auto switchLanguage = [chineseAction, englishAction](MOON::Language::ELanguage p_language) {
				MOON::Language::Set(p_language);
				MOON::Language::Apply(qApp);
				chineseAction->setChecked(p_language == MOON::Language::ELanguage::Chinese);
				englishAction->setChecked(p_language == MOON::Language::ELanguage::English);
			};
			QObject::connect(chineseAction, &QAction::triggered, self,
				[switchLanguage] { switchLanguage(MOON::Language::ELanguage::Chinese); });
			QObject::connect(englishAction, &QAction::triggered, self,
				[switchLanguage] { switchLanguage(MOON::Language::ELanguage::English); });
		}
		void buildSketchMenu() {
			auto sketchCommand = new SketchCommand(self);
			menu_sketch->addAction(sketchCommand->action());
		}
		void buildMenu() {
			// The menu bar and the ribbon tabs share one row. The tabs are part of the
			// top bar rather than a toolbar of their own, so the ribbon starts right
			// under the menus instead of two strips below them.
			mTopBar = new QWidget(self);
			mTopBar->setObjectName("ribbonTopBar");
			// The template is kept on the widget so Theme::Apply can re-evaluate it when the
			// colour scheme changes (see the moonStyle loop there).
			mTopBar->setProperty("moonStyle", QStringLiteral(
				"#ribbonTopBar {"
				"  background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
				"    stop:0 @barTop, stop:1 @barBottom);"
				"  border-bottom: 2px solid @barBorder; }"
				"QMenuBar { background: transparent; border: none; }"));
			mTopBar->setStyleSheet(MOON::Theme::Style(mTopBar->property("moonStyle").toString()));
			auto* topLayout = new QHBoxLayout(mTopBar);
			topLayout->setContentsMargins(0, 0, 0, 0);
			topLayout->setSpacing(4);

			mMenubar = new QMenuBar(mTopBar);
			mMenubar->setObjectName(QString::fromUtf8("menubar"));
			topLayout->addWidget(mMenubar, 0);

			mTabs = new QTabBar(mTopBar);
			mTabs->setDocumentMode(true);
			mTabs->setDrawBase(false);
			mTabs->setExpanding(false);
			mTabs->setFocusPolicy(Qt::NoFocus);
			mTabs->setProperty("moonStyle", QStringLiteral(
				"QTabBar { background: transparent; }"
				"QTabBar::tab { background: transparent; color: @textDim; padding: 4px 14px;"
				"  margin-right: 2px; border: none; border-bottom: 2px solid transparent; }"
				"QTabBar::tab:hover { color: @text; }"
				"QTabBar::tab:selected { color: @text; border-bottom: 2px solid @accent;"
				"  background: @hoverWash; }"));
			mTabs->setStyleSheet(MOON::Theme::Style(mTabs->property("moonStyle").toString()));
			topLayout->addWidget(mTabs, 0, Qt::AlignVCenter);
			topLayout->addStretch(1);
			self->setMenuWidget(mTopBar);

			menu_File = new QMenu(mMenubar);
			menu_Display = new QMenu(mMenubar);
			menu_View = new QMenu(mMenubar);
			menu_sketch = new QMenu(mMenubar);
			// Menu titles carry both spellings, so switching the language rewrites the bar too.
			MOON::Language::SetLabel(menu_File->menuAction(), QStringLiteral("文件"), QStringLiteral("File"));
			MOON::Language::SetLabel(menu_Display->menuAction(), QStringLiteral("显示"), QStringLiteral("Display"));
			MOON::Language::SetLabel(menu_View->menuAction(), QStringLiteral("视图"), QStringLiteral("View"));
			MOON::Language::SetLabel(menu_sketch->menuAction(), QStringLiteral("草图"), QStringLiteral("Sketch"));
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
			// The ribbon has two pages: the sketch page carries the drawing tools, the
			// edit tools, the references and the constraints (a sketch needs them
			// together), and the model page the features that turn a sketch into a
			// solid. Groups are two rows tall, which is what lets the whole sketch page
			// fit in one row of groups.
			auto* sketchToolbar = new SketchToolbar(self);
			auto* primitiveToolbar = new PrimitiveToolbar(self);
			auto* designModelingToolbar = new DesignModelingToolbar(self);

			const QList<QToolBar*> pageToolbars = {
				sketchToolbar, primitiveToolbar, designModelingToolbar };
			for (QToolBar* toolbar : pageToolbars) {
				toolbar->layout()->setSpacing(0);
				self->addToolBar(Qt::TopToolBarArea, toolbar);
			}

			// Each tab remembers both spellings; Language::Apply rewrites them on a switch.
			mTabs->setProperty("moonTabs", QVariantList{
				QStringLiteral("草图|Sketch"), QStringLiteral("模型|Model") });
			mTabs->addTab(MOON::Language::Pick(QStringLiteral("草图"), QStringLiteral("Sketch")));
			mTabs->addTab(MOON::Language::Pick(QStringLiteral("模型"), QStringLiteral("Model")));
			const auto showPage = [=](int p_index) {
				sketchToolbar->setVisible(p_index == 0);
				const bool modeling = p_index == 1;
				primitiveToolbar->setVisible(modeling);
				designModelingToolbar->setVisible(modeling);
			};
			QObject::connect(mTabs, &QTabBar::currentChanged, self, showPage);
			// Which page comes up first. MOON_RIBBON_PAGE overrides it, which is how a review
			// screenshot of the sketch page can be taken without anyone clicking the tab.
			int initialPage = 1;
			if (const char* p_page = std::getenv("MOON_RIBBON_PAGE")) {
				initialPage = std::atoi(p_page);
			}
			mTabs->setCurrentIndex(initialPage);
			showPage(initialPage);

		}
		void retranslateUi() {
			
			self->setWindowTitle(QStringLiteral("MOON"));
			// Through the language helper: a plain setTitle here would overwrite what buildMenu
			// put on the menu actions, and the bar would go back to English.
			MOON::Language::SetLabel(menu_File->menuAction(), QStringLiteral("文件"), QStringLiteral("File"));
			MOON::Language::SetLabel(menu_Display->menuAction(), QStringLiteral("显示"), QStringLiteral("Display"));
			MOON::Language::SetLabel(menu_View->menuAction(), QStringLiteral("视图"), QStringLiteral("View"));
			MOON::Language::SetLabel(menu_sketch->menuAction(), QStringLiteral("草图"), QStringLiteral("Sketch"));
		
		}
	private:
		Editor* self = nullptr;
		QSplitter* vert_splitter_ = nullptr;
		QMenuBar* mMenubar;
		/** The menu bar and the ribbon tabs, as one row (see buildMenu). */
		QWidget* mTopBar = nullptr;
		QTabBar* mTabs = nullptr;
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
