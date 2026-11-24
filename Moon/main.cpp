#include "resource/DarkStyle.h"
#include "editor/Language.h"
#include "editor/ViewCubeLabels.h"
#include "editor/Theme.h"
#include "core/log.h"
#include "GeometryInit.h"
#include "core/JobSystem.h"
#include <QApplication>
#include <QMenuBar>
#include <QScreen>
#include <QTabBar>
#include <QDockWidget>
#include <QPixmap>
#include <QFileInfo>
#include <QFontDatabase>
#include <QImage>
#include <QStringList>
#include <QTimer>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#include <editor/editor.h>
#include "Core/Global/ServiceLocator.h"
#include "editor/DocumentSession.h"
#include "editor/UI/HomePanel/RecentProjects.h"
#include "editor/View/sceneview/viewerwidget.h"
#include "feature/MoonDocument.h"

int main(int argc, char* argv[])
{
	// A fatal fault (an access violation, a debug-CRT abort from a failed assert, a breakpoint)
	// ends the process with nothing on stderr. Print the faulting thread's stack with file and
	// line instead: the PDB is next to the executable the run was started from.
	SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* p_info) -> LONG {
		std::fprintf(stderr, "[fatal] exception 0x%08lx at %p\n",
			static_cast<unsigned long>(p_info->ExceptionRecord->ExceptionCode),
			p_info->ExceptionRecord->ExceptionAddress);
		void* frames[64] = {};
		const USHORT count = CaptureStackBackTrace(0, 64, frames, nullptr);
		HANDLE process = GetCurrentProcess();
		SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
		SymInitialize(process, nullptr, TRUE);
		for (USHORT i = 0; i < count; ++i)
		{
			char buffer[sizeof(SYMBOL_INFO) + 512] = {};
			SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
			symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
			symbol->MaxNameLen = 511;
			DWORD64 displacement = 0;
			DWORD lineDisplacement = 0;
			IMAGEHLP_LINE64 line{ sizeof(IMAGEHLP_LINE64) };
			if (SymFromAddr(process, reinterpret_cast<DWORD64>(frames[i]), &displacement, symbol))
			{
				const bool hasLine = SymGetLineFromAddr64(process,
					reinterpret_cast<DWORD64>(frames[i]), &lineDisplacement, &line) != FALSE;
				std::fprintf(stderr, "  #%02u %s+0x%llx  %s(%lu)\n", i, symbol->Name,
					static_cast<unsigned long long>(displacement),
					hasLine ? line.FileName : "?", hasLine ? line.LineNumber : 0);
			}
			else
			{
				std::fprintf(stderr, "  #%02u %p\n", i, frames[i]);
			}
		}
		std::fflush(stderr);
		SymCleanup(process);
		return EXCEPTION_CONTINUE_SEARCH;
	});

	// An unhandled C++ exception ends the process with 0xe06d7363 and no message at all in a
	// release-style console, which says nothing about what threw. Report the type and the message
	// on the way out, so a fatal `bad_any_cast` or `bad_optional_access` names itself.
	std::set_terminate([] {
		if (std::exception_ptr thrown = std::current_exception())
		{
			try
			{
				std::rethrow_exception(thrown);
			}
			catch (const std::exception& p_error)
			{
				std::fprintf(stderr, "[terminate] unhandled C++ exception: %s\n", p_error.what());
			}
			catch (...)
			{
				std::fprintf(stderr, "[terminate] unhandled exception of a non-std type\n");
			}
		}
		else
		{
			// Nothing is in flight: std::terminate was reached through a `noexcept` boundary (or an
			// explicit call), and the type is already lost by then.
			std::fprintf(stderr, "[terminate] no active exception (noexcept boundary)\n");
		}
		std::fflush(stderr);
		std::abort();
	});

	// Every GL context in the editor has to be in one share group. The render module draws on a
	// context of its own, and the material panel shows the preview by blitting the module's
	// frame texture into a widget - a texture name only means something in the group that
	// created it. This attribute is what makes Qt's QOpenGLWidget contexts (the viewport, the
	// material preview) share Qt's global context, and the render module is created in that
	// group too (see Editor::Core::RenderModuleHost). It has to be set before the
	// application exists.
	QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
	QApplication app(argc, argv);
	// TEMPORARY PROBE: MOON_MENU_SHOT=<png> opens the first menu and grabs the screen, so a menu
	// can be reviewed without anyone clicking. Remove with the other probes.
	if (const char* p_menuShot = std::getenv("MOON_MENU_SHOT"); p_menuShot != nullptr && *p_menuShot != '\0') {
		const QString target = QString::fromLocal8Bit(p_menuShot);
		QTimer::singleShot(3500, &app, [target, &app] {
			for (QWidget* window : QApplication::topLevelWidgets()) {
				if (auto* bar = window->findChild<QMenuBar*>()) {
					if (bar->actions().isEmpty()) { continue; }
					if (QMenu* menu = bar->actions().first()->menu()) {
						menu->popup(bar->mapToGlobal(QPoint(0, bar->height())));
					}
					break;
				}
			}
			QTimer::singleShot(600, &app, [target, &app] {
				if (QScreen* screen = QApplication::primaryScreen()) {
					screen->grabWindow(0).save(target);
					CORE_INFO("[UI] menu screenshot saved to {0}", target.toStdString());
				}
			});
		});
	}
	MOON::Language::Initialize();
	MOON::Theme::Initialize();


	// TEMPORARY PROBE: MOON_VCUBE_ATLAS=<png> dumps the runtime view-cube label atlas and exits.
	if (const char* p_atlas = std::getenv("MOON_VCUBE_ATLAS"); p_atlas != nullptr && *p_atlas != '\0') {
		const QImage atlas = MOON::ViewCubeLabels::BuildAtlas(256);
		atlas.save(QString::fromLocal8Bit(p_atlas));
		return 0;
	}
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
	// The window/taskbar icon: a crescent moon, after the project's name. Every top-level
	// window without an icon of its own picks this up - the editor, the dialogs, and the
	// floating dock panels.
	QApplication::setWindowIcon(MOON::Theme::Icon(":/icons/app/moon.svg"));
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
		// Starts maximized: the viewport is the thing being looked at, and a window that opens at
		// 1920x1080 on a smaller desktop is not the same picture (docs/RenderThreadPlan.md §8.69).
		editor.setWindowState(editor.windowState() | Qt::WindowMaximized);
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
			QTimer::singleShot(200, &app, [project] {
				GetService(MOON::DocumentSession).Open(project);
				});
		}
		// TEMPORARY PROBE: a run that saves its frame (MOON_SHOT) closes itself, so the save on
		// the way out happens without anyone clicking. Delay is the time the read and the frame
		// get; see ViewportOverlay::ApplyClipDemo.
		if (std::getenv("MOON_SHOT") != nullptr) {
			const char* p_delay = std::getenv("MOON_SHOT_DELAY_MS");
			const int delay = p_delay != nullptr ? std::atoi(p_delay) : 8000;
			QTimer::singleShot(delay, &app, [] { QCoreApplication::quit(); });
		}
		res = QApplication::exec();
		// The picture of the document that is open is taken here, the way closing a project
		// takes one: the viewport is still drawing it, so what is grabbed is how it was left.
		// A document that was only ever opened has no picture otherwise - the one its card is
		// shown by is written when a document is written or closed - and it would come back as
		// a card with a name and nothing on it.
		const QString document = GetService(MOON::DocumentSession).Path();
		if (!document.isEmpty()) {
			MOON::RecentProjects::Remember(document, MOON::RecentProjects::GrabViewportPreview());
		}
		// TEMPORARY PROBE (see ViewportOverlay::ApplyClipDemo): MOON_SHOT=<png> saves the
		// viewport's frame on the way out, so two headless runs (a section with and without
		// something) can be compared pixel by pixel. Remove with the rest of the probe.
		if (const char* p_windowShot = std::getenv("MOON_WINDOW_SHOT"); p_windowShot != nullptr && *p_windowShot != '\0') {
			const QPixmap window = GetViewerWidget.window()->grab();
			if (!window.isNull()) {
				window.save(QString::fromLocal8Bit(p_windowShot));
				CORE_INFO("[UI] window saved to {0}", p_windowShot);
			}
		}
		if (const char* p_shot = std::getenv("MOON_SHOT"); p_shot != nullptr && *p_shot != '\0') {
			const QImage frame = GetViewerWidget.grabFramebuffer();
			if (!frame.isNull()) {
				frame.save(QString::fromLocal8Bit(p_shot));
				CORE_INFO("[Section] viewport saved to {0}", p_shot);
			}
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
