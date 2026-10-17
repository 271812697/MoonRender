#include "resource/DarkStyle.h"
#include "core/log.h"
#include "GeometryInit.h"
#include "core/JobSystem.h"
#include <QApplication>
#include <QFileInfo>
#include <QFontDatabase>
#include <QStringList>
#include <QTimer>
#include <editor/editor.h>
#include "Core/Global/ServiceLocator.h"
#include "editor/UI/HomePanel/RecentProjects.h"
#include "editor/View/sceneview/viewerwidget.h"
#include "feature/MoonDocument.h"

int main(int argc, char* argv[])
{
	QApplication app(argc, argv);
	app.setStyle(new DarkStyle);
	const int font_id = QFontDatabase::addApplicationFont(
		":/darkstyle/Ubuntu-R.ttf");
	QFont font(QFontDatabase::applicationFontFamilies(font_id).at(0));
	font.setPointSize(9);
	MOON::Log::Init();
	CORE_INFO("start ");
	CORE_INFO("Init Geometry Types");
	Part::GeometryTypeInit();
	//test jobsystem
	MOON::System::JobSystem::OnInit(2);
	
	QApplication::setFont(font);
	// The editor is scoped on purpose. Its destructor is what takes the viewport, the
	// renderer, the GL context and every panel down, and all of that has to happen while
	// the logger and the job system are still up: a widget that logs on the way out, or
	// posts one last job, used to run after Release() and Shutdown() had already happened
	// - which is a null job system and a shut-down logger, on the most fragile path there
	// is. Taking the window down first and the services last is the order that leaves
	// nothing running against something that is already gone.
	int res = 0;
	{
		MOON::Editor editor;
		editor.setWindowTitle("MOON");
		editor.resize(1920, 1080);
		editor.show();
		// A project named on the command line is opened as soon as the event loop runs: the
		// viewport builds its GL context on the first show, and a document is only loaded
		// into one that has it - the same reason the start page opens a project by signal
		// from inside the loop rather than from the constructor. Short delay on top of that,
		// so the first show (and the context it makes) has actually happened. This is what
		// lets a scripted run render a document without anyone clicking: the shader passes
		// are only compiled once something is being drawn.
		const QStringList arguments = QApplication::arguments();
		if (arguments.size() > 1 && QFileInfo::exists(arguments[1])) {
			const QString project = arguments[1];
			QTimer::singleShot(200, &app, [project] { GetViewerWidget.onReadFile(project); });
		}
		res = QApplication::exec();
		// The picture of the document that is open is taken here, the way closing a project
		// takes one: the viewport is still drawing it, so what is grabbed is how it was left.
		// A document that was only ever opened has no picture otherwise - the one its card is
		// shown by is written when a document is written or closed - and it would come back as
		// a card with a name and nothing on it.
		const QString document = GetViewerWidget.getDocumentPath();
		if (!document.isEmpty()) {
			MOON::RecentProjects::Remember(document, MOON::RecentProjects::GrabViewportPreview());
		}
		// The document is closed on the way out, the way the File menu closes one: its bodies
		// go, with their features, the actors those spawned, and the sketches that leave the
		// sketch manager with them. Doing it here rather than leaving it to the statics is what
		// keeps all of that happening while the editor and its scene are still up; the bodies
		// are held in a static list, and one that outlives its scene has nothing left to take
		// the node it is shown by out of.
		MOON::MoonDocument::close();
	}
	// Last, once no widget is left: the job system joins its workers on the way out (see
	// JobSystem::InternalState::stop()), which only means anything if nothing can queue
	// more work behind it.
	MOON::System::JobSystem::Release();
	MOON::Log::Shutdown();
	return res;
}
