#include "viewportframe.h"

#include "viewportoverlay.h"
#include "qtinputtranslator.h"

#include "editor/DocumentSession.h"
#include "editor/UI/TreeViewPanel/treeViewpanel.h"
#include "editor/UI/TreeViewPanel/treesync.h"
#include "editor/UI/SettingPanel/PassSettingWidget.h"
#include "renderer/RenderModuleHost.h"
#include "renderer/SceneView.h"
#include "renderer/Context.h"
#include "Interactive/Im3DRenderer.h"
#include "Rendering/Backend/DeferredDeleter.h"

#include "Interactive/Interactive/RenderWindowInteractor.h"
#include "Core/Global/ServiceLocator.h"
#include "core/SelectionManager.h"
#include "core/log.h"

#include <QEvent>
#include <QWidget>

#include <algorithm>

namespace MOON
{
	ViewportFrame::ViewportFrame(QWidget& p_owner)
		: m_overlay(new ViewportOverlay)
	{
		Editor::Core::RenderModuleHost* module = Editor::Core::RenderModuleHost::Instance();
		// Creating a view records commands - front-end framebuffers/textures take handles, and the
		// GL objects behind them are made by the executor - so it needs no context of its own, and
		// it must not borrow the module's: that one belongs to the executor. The view's first
		// frame records "activate the context" as its first command (AView::Render ->
		// Driver::MakeCurrent), which is where filament puts it too (DriverApi::makeCurrent).
		if (module != nullptr && module->IsReady())
		{
			m_view = new Editor::Panels::SceneView("SceneView");
		}
		if (m_view == nullptr)
		{
			CORE_ERROR("[Viewer] no render module to create the scene view on");
		}
		ConnectTreeSelection(p_owner);
	}

	ViewportFrame::~ViewportFrame()
	{
		if (m_view == nullptr)
		{
			return;
		}

		// Taking the view down releases front-end objects, and their HAL destructors record the
		// destroy commands (the executor deletes the GL objects) - so this needs no context either.
		delete m_view;
		m_view = nullptr;
		// The view registered itself as a service; nothing may be left pointing at it.
		::Core::Global::ServiceLocator::Remove<Editor::Panels::SceneView>();
	}

	void ViewportFrame::ConnectTreeSelection(QWidget& p_owner)
	{
		// Guarded: the viewport can be built before the tree panel is (it is created while
		// the panels are being built), and asking the service locator for a panel that is
		// not there throws - which in a release build takes the process down silently.
		if (!::Core::Global::ServiceLocator::IsProvided<TreeViewPanel>())
		{
			return;
		}

		auto& tree = GetTreeView;
		QObject::connect(&tree, &TreeViewPanel::setSelectActor, &p_owner,
			[this](Core::ECS::Actor* p_actor) { OnActorSelected(p_actor); });
		QObject::connect(&tree, &TreeViewPanel::itemHovered, &p_owner,
			[this](Core::ECS::Actor* p_actor) { OnActorHovered(p_actor); });
		QObject::connect(&tree, &TreeViewPanel::itemLeave, &p_owner,
			[this](Core::ECS::Actor* p_actor) { OnActorHoverLeaved(p_actor); });
	}

	void ViewportFrame::OnActorSelected(Core::ECS::Actor* p_actor)
	{
		if (p_actor == nullptr)
		{
			return;
		}
		if (auto* view = View())
		{
			view->SelectActor(*p_actor);
		}
		GetSelection.select({ p_actor->GetID() });
	}

	void ViewportFrame::OnActorHovered(Core::ECS::Actor* p_actor)
	{
		if (p_actor != nullptr)
		{
			GetSelection.setPreselect(p_actor->GetID());
		}
	}

	void ViewportFrame::OnActorHoverLeaved(Core::ECS::Actor* p_actor)
	{
		if (p_actor != nullptr)
		{
			GetSelection.clearPreselect();
		}
	}

	Editor::Panels::SceneView* ViewportFrame::View() const
	{
		return m_view;
	}

	DocumentSession* ViewportFrame::Session()
	{
		if (m_session == nullptr && ::Core::Global::ServiceLocator::IsProvided<DocumentSession>())
		{
			m_session = &GetService(DocumentSession);
		}
		return m_session;
	}

	void ViewportFrame::SetSurfaceSize(int p_width, int p_height)
	{
		m_width = std::max(1, p_width);
		m_height = std::max(1, p_height);
		// The view is created one event-loop turn after the widget is first shown, so it
		// misses the resizes that happened before it existed: without this it renders at its
		// 1x1 construction size and the window shows nothing.
		if (auto* view = View())
		{
			view->Resize(m_width, m_height);
		}
		RenderWindowInteractor::Instance()->UpdateSize(m_width, m_height);
	}

	void ViewportFrame::HandleEvent(QEvent* p_event)
	{
		if (auto* view = View())
		{
			::Editor::Panels::InputEvent inputEvent;
			if (ViewportInput::TranslateQtEvent(p_event, inputEvent))
			{
				view->ReceiveEvent(inputEvent);
			}
		}
		// OCCT's interactor is UI glue and keeps taking the Qt event as is.
		RenderWindowInteractor::Instance()->ReceiveEvent(p_event);
	}

	void ViewportFrame::ClearHover()
	{
		GetSelection.clearPreselect();
		if (auto* view = View())
		{
			view->ClearHoverHighlight();
		}
	}

namespace
{
	/** MOON_FRAME_TRACE prints the frame's milestones to stderr: the bring-up tool for finding out
	 * which step of a frame blocks when the render thread owns the context. */
	void TraceFrame(const char* p_what)
	{
		static const bool enabled = std::getenv("MOON_FRAME_TRACE") != nullptr;
		if (!enabled)
		{
			return;
		}
		std::fprintf(stderr, "[frame] %s\n", p_what);
		std::fflush(stderr);
	}
}
	ViewportFrame::Result ViewportFrame::Render()
	{
	
		Result result;
		Editor::Panels::SceneView* view = m_view;
		Editor::Core::RenderModuleHost* module = Editor::Core::RenderModuleHost::Instance();
		// A paint can land before the module (and with it the view) exists.
		if (view == nullptr || module == nullptr)
		{
			return result;
		}
		module->GetContext()->driver->MakeCurrent();

		SyncViewSize();

		// Minimal closed loop for the render-thread bring-up: with MOON_RENDER_MINIMAL the frame
		// records nothing and draws nothing on this thread, so all that is exercised is the engine's
		// own thread - the driver on it, the loop, the teardown. Whatever breaks after this point is
		// in the frame's content, not in the threading.

		// Minimal frame for the render-thread bring-up: the content is the scene block and the
		// present, recorded by AView::Render; the overlays and the deferred uploads stay out (they
		// are the next layer). MOON_RENDER_MINIMAL only has meaning with a render thread.
		const bool minimalFrame = std::getenv("MOON_RENDER_MINIMAL") != nullptr && module->IsThreaded();

		// The overlays are the last layer that still issues its own GL (Im2D's draw list, ImGui).
		// With a render thread they would run on the recording side, where there is no context -
		// so they stay off until their GL is recorded as commands, and the frame is the scene, the
		// 3D overlay (gizmos, view cube: those are commands now) and the present.
		const bool overlayOff = minimalFrame || module->IsThreaded();



		// The frame needs the module's context on the thread that owns it: the render thread when
		// the module has one, otherwise this (the GUI) thread. Everything the frame does - the
		// document read that uploads meshes, the update's queries, the render and the overlays -
		// is inside the job, so it all happens with that context current.
		auto renderJob = [this, view, module, minimalFrame, overlayOff]
		{
			// Resource work queued since the last frame - uploads and the destruction of what was
			// released - runs before anything is drawn: the overlays and the gizmos draw outside
			// the command path, so they cannot rely on a drain having happened first.
			if (!overlayOff)
			{
				Rendering::Backend::DeferredDeleter::Flush();
			}
			TraceFrame("frame: enter");

			// The gizmo's geometry is built here, on the first frame - not during the module build.
			// It is handles and commands now (Rendering::Overlay::Im3DGLRenderer), so it no longer
			// needs the module's context on this thread and runs in both modes.
			if (!minimalFrame && !m_gizmoReady)
			{
				m_gizmoReady = true;
				MOON::ImRenderer::instance().initGizmoGl();
				TraceFrame("initGizmoGl");
				Rendering::Backend::DeferredDeleter::SetUploadsDeferrable(true);
			}

			if (!overlayOff)
			{
				//m_overlay->BeginFrame(*view);
			}
			else if (!minimalFrame)
			{
				// With a render thread only the command-recorded overlay layers run: the 3D one
				// (gizmos, view cube) and the 2D draw list (the widgets' icons and lines). ImGui
				// still issues its own GL, so its frame stays closed.
				//m_overlay->BeginGizmoFrame(*view);
				//m_overlay->BeginOverlay2D(*view);
			}
			TraceFrame("overlay->BeginFrame");

			// The document read is here because it needs everything the frame has: the scene, the
			TraceFrame("FlushDocumentRead");
			// view and the module's context. Building a feature chain is only allowed in this
			// loop, and a step import starts jobs that only the frames below can wait for.
			FlushDocumentRead();
			TraceFrame("view->Update");
			// ... and ahead of the update, so the query and the fit a read asks for happen in
			// this frame rather than the next one (the view ticks its pending fit in Update).
			view->Update(0.01f);

			const bool traceFrame = m_traceNextFrame || view->ShouldTraceFitFrames();
			m_traceNextFrame = false;
			if (traceFrame)
			{
				CORE_INFO("[Viewer] drawing a frame ({0} fit frame(s) left)",
					view->IsFitPending() ? view->PendingFitFrames() : 0);
			}

			// The overlays are drawn *inside* the frame, into the module's present target: ImGui,
			// the 2D draw list and the gizmos are part of what the window pastes, not something
			TraceFrame("view->Render");
			// the window draws afterwards on its own context. That is what leaves the module
			// free of the window layer - it draws its overlays wherever it runs.
			view->Render([this, view, module, minimalFrame, overlayOff]
			{
				// The 2D overlay records its state setup and its draws on the module's command
				// stream and drains it there (see Im2DRender::EndFrame).
				if (!overlayOff)
				{
					//m_overlay->EndFrame(*view, *module->GetContext()->driver);
				}
				else if (!minimalFrame)
				{
					//m_overlay->EndOverlay2D(*view, *module->GetContext()->driver);
				}
			});
			if (traceFrame)
			{
				CORE_INFO("[Viewer] frame drawn");
			}
		};

		// Recording only: no context is made current here in either mode. The module's context
		// belongs to the executor and is activated by the frame's own first command
		// (Driver::MakeCurrent). In the single-threaded configuration the frame executes on this
		// thread inside AView::Render, and the executor activates the context there; the window
		// rebinds its own context before it pastes (ViewerWidget::paintGL).
		renderJob();
		// GUI-thread work that the frame asked for: the tree panel is a Qt model and stays here.
		TreeViewSync::Instance().Flush();
		ReadPassSettingsOnce();
		if (m_treeRefreshPending)
		{
			m_treeRefreshPending = false;
			TreeViewSync::Instance().Refresh();
		}

		const auto [frameWidth, frameHeight] = view->GetSafeSize();
		// The swap chain's ordering (the Qt/Filament demo's paintGL): the frame about to be pasted
		// is the last one that was committed, never the one the render thread is drawing right now.
		
		result.texture = view->GetFrameTextureID();
		result.width = frameWidth;
		result.height = frameHeight;
		if (std::getenv("MOON_FRAME_TRACE") != nullptr)
		{
			std::fprintf(stderr, "[paste] texture=%u %dx%d threaded=%d\n", result.texture,
				frameWidth, frameHeight, module->IsThreaded() ? 1 : 0);
			std::fflush(stderr);
		}
		// One frame of input has been consumed: the state the camera controller reads is
		// cleared here, so a key that is pressed and released between two frames is not
		// replayed on the next one.
		view->getInutState().ClearEvents();
		return result;
	}

	void ViewportFrame::FlushDocumentRead()
	{
		DocumentSession* session = Session();
		Editor::Panels::SceneView* view = View();
		if (session == nullptr || view == nullptr)
		{
			return;
		}

		const DocumentSession::ReadResult read = session->FlushPendingRead();
		if (!read.read)
		{
			return;
		}

		m_traceNextFrame = true;
		view->UnselectActor();
		// A file that was just read arrives without scene query structures and with the
		// camera still on the previous one, so it has to be built and framed. The view waits
		// for geometry to frame itself (see SceneView::RequestFitWhenReady), and the clip
		// demo probe runs on the frame that did it.
		view->RequestFitWhenReady([view]
			{
				ViewportOverlay::ApplyClipDemo(*view);
			});
		if (read.refreshTree)
		{
			// Discretizing a shape rebuilds the topology actors under it (Solid/Shell/Face_*/
			// Edge_*), so the tree has to be rebuilt rather than patched. The tree is a Qt model,
			// so the rebuild itself happens on the GUI thread, after this job (see Render).
			m_treeRefreshPending = true;
		}
	}

	void ViewportFrame::ReadPassSettingsOnce()
	{
		// The render pass panel lists the view's passes, so it can only be filled once there
		// is a view. Doing it here rather than while the module is built is what keeps the
		// module free of the panels: a missing panel is skipped, not a crash on start-up.
		if (m_passSettingsRead || !::Core::Global::ServiceLocator::IsProvided<RenderPassSettingWidget>())
		{
			return;
		}
		GetService(RenderPassSettingWidget).Refresh();
		m_passSettingsRead = true;
	}

	void ViewportFrame::SyncViewSize()
	{
		auto* view = View();
		if (view == nullptr)
		{
			return;
		}
		const auto [viewWidth, viewHeight] = view->GetSafeSize();
		if (static_cast<int>(viewWidth) != m_width || static_cast<int>(viewHeight) != m_height)
		{
			view->Resize(m_width, m_height);
		}
	}
}
