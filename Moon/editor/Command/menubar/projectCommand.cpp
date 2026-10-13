#include "editor/Command/menubar/projectCommand.h"

#include <QAction>

#include "Core/Global/ServiceLocator.h"
#include "core/callbackManager.h"
#include "core/log.h"
#include "editor/Command/menubar/saveFile.h"
#include "editor/UI/HomePanel/HomePanel.h"
#include "editor/UI/HomePanel/RecentProjects.h"
#include "editor/View/sceneview/viewerwidget.h"
#include "feature/FeatureBody.h"
#include "feature/MoonDocument.h"

namespace MOON
{
	NewProjectCommand::NewProjectCommand(QObject* parentObject)
		: Command(parentObject)
	{
		auto* action = new QAction(this);
		setAction(action);
		action->setObjectName(QString::fromUtf8("actionNewProject"));
		action->setText(QString::fromUtf8("New &Project"));
		action->setStatusTip("Start a project: an empty scene with one body in it");
		action->setShortcut(QKeySequence::New);
	}

	void NewProjectCommand::execute()
	{
		// Whatever was up goes: a project is one document, and the scene a document is
		// edited in is the one that is open.
		MoonDocument::close();
		SaveFileCommand::ResetPath();
		// The workspace comes up first, and the body is made after that, once the viewport
		// that owns the scene is there: the scene of a project belongs to the viewport,
		// and it is only registered with the service locator once that viewport has come
		// up. A body made before that has no scene to be shown in - and asking for the
		// service the scene lives in by then throws, which is what used to take the
		// application down on the way into a new project. The queue below is drained on
		// the viewport's first frame (see CallBackManager).
		GetService(HomePanel).ShowWorkspace();
		createCallBack(CallBackManager::instance(), []() {
			// One body to start from: a feature has to belong to a body, and the body is
			// what the first one joins.
			FeatureBody::Create(FeatureBody::UniqueName("Body"));
			GetViewerWidget.refreshTreeView();
			CORE_INFO("[Project] a new project was started");
			});
	}

	CloseProjectCommand::CloseProjectCommand(QObject* parentObject)
		: Command(parentObject)
	{
		auto* action = new QAction(this);
		setAction(action);
		action->setObjectName(QString::fromUtf8("actionCloseProject"));
		action->setText(QString::fromUtf8("&Close Project"));
		action->setStatusTip("Close the project and go back to the start page");
	}

	void CloseProjectCommand::execute()
	{
		// The project is kept as a recent one with a picture of how it was left, so
		// the start page shows the state it was closed in - a project that was never
		// written has no file to remember, and is only closed.
		const QString document = SaveFileCommand::CurrentPath();
		if (!document.isEmpty()) {
			RecentProjects::Remember(document, RecentProjects::GrabViewportPreview());
		}
		MoonDocument::close();
		SaveFileCommand::ResetPath();
		// The document that was open goes with it, so the file the viewport was read from
		// is forgotten here too: whoever looks at it next - the picture taken on the way
		// out, for one - has to be able to tell that nothing is open right now.
		GetViewerWidget.clearDocumentPath();
		GetViewerWidget.refreshTreeView();
		GetService(HomePanel).ShowStartPage();
		CORE_INFO(
			"[Project] '{0}' was closed",
			document.isEmpty() ? std::string("(never saved)") : document.toStdString());
	}
}
