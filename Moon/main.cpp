#include "resource/DarkStyle.h"
#include "core/log.h"
#include "GeometryInit.h"
#include "core/JobSystem.h"
#include <QApplication>
#include <QFontDatabase>
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
	MOON::Editor editor;
	editor.setWindowTitle("MOON");
	editor.resize(1920, 1080);
	editor.show();
	int res = QApplication::exec();
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
	MOON::System::JobSystem::Release();
	MOON::Log::Shutdown();
	return 0;
}
