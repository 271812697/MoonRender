#include "viewportframe.h"

#include "viewportoverlay.h"
#include "qtinputtranslator.h"

#include "editor/DocumentSession.h"
#include "editor/UI/TreeViewPanel/treeViewpanel.h"
#include "editor/UI/TreeViewPanel/treesync.h"
#include "editor/UI/SettingPanel/PassSettingWidget.h"
#include "renderer/RenderModuleHost.h"
#include "renderer/SceneView.h"

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
		if (module != nullptr && module->IsReady() && module->MakeCurrent())
		{
			// A view owns framebuffers and render targets, and those belong to the context that
			// draws with them: the scene view is created with the module's context current, and
			// it is taken down the same way. Without a context of its own the module renders on
			// the widget's, which its owner has current already (see ViewerWidget).
			m_view = new Editor::Panels::SceneView("SceneView");
			module->DoneCurrent();
		}
		else
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

		// The view's render targets are the module context's objects, so they go with that
		// context current - and only while the module still exists.
		Editor::Core::RenderModuleHost* module = Editor::Core::RenderModuleHost::Instance();
		if (module != nullptr)
		{
			module->MakeCurrent();
		}
		delete m_view;
		m_view = nullptr;
		if (module != nullptr)
		{
			module->DoneCurrent();
		}
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

		SyncViewSize();
		if (!module->MakeCurrent())
		{
			return result;
		}

		m_overlay->BeginFrame(*view);

		// The document read is here because it needs everything the frame has: the scene, the
		// view and the module's context. Building a feature chain is only allowed in this
		// loop, and a step import starts jobs that only the frames below can wait for.
		FlushDocumentRead();
		// ... and ahead of the update, so the query and the fit a read asks for happen in
		// this frame rather than the next one (the view ticks its pending fit in Update).
		view->Update(0.01f);
		TreeViewSync::Instance().Flush();
		ReadPassSettingsOnce();

		const bool traceFrame = m_traceNextFrame || view->ShouldTraceFitFrames();
		m_traceNextFrame = false;
		if (traceFrame)
		{
			CORE_INFO("[Viewer] drawing a frame ({0} fit frame(s) left)",
				view->IsFitPending() ? view->PendingFitFrames() : 0);
		}

		// The overlays are drawn *inside* the frame, into the module's present target: ImGui,
		// the 2D draw list and the gizmos are part of what the window pastes, not something
		// the window draws afterwards on its own context. That is what leaves the module
		// free of the window layer - it draws its overlays wherever it runs.
		view->Render([this, view] {
			m_overlay->EndFrame(*view);
			});
		if (traceFrame)
		{
			CORE_INFO("[Viewer] frame drawn");
		}

		module->DoneCurrent();

		const auto [frameWidth, frameHeight] = view->GetSafeSize();
		result.texture = view->GetFrameTextureID();
		result.width = frameWidth;
		result.height = frameHeight;
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
			// Edge_*), so the tree has to be rebuilt rather than patched.
			TreeViewSync::Instance().Refresh();
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
