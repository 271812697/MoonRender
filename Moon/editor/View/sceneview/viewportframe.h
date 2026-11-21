#pragma once

#include <cstdint>
#include <memory>

class QEvent;
class QWidget;

namespace Core::ECS
{
	class Actor;
}
namespace Editor::Panels
{
	class SceneView;
}

namespace MOON
{
	class DocumentSession;
	class ViewportOverlay;

	/** One frame of the scene viewport, and the state a frame carries between frames.
	 *
	 * This is the window layer's half of rendering, and it is deliberately the half that
	 * owns no window: it makes the render module's context current, steps the view (the
	 * pending document read, the scene update and its pending fit, the pending scene-tree
	 * edits), draws the view with the overlays inside the frame, and closes the context.
	 * What it does not do is show the result - that is a blit on the window's own context,
	 * and it belongs to the window (see ViewerWidget::paintGL).
	 *
	 * It owns the scene view, because the view is this viewport's: its render targets are
	 * drawn on the render module's context and belong to it, but they are only used here.
	 * The module itself (the context and the editors Context) is a singleton every view
	 * shares - see RenderModuleHost - and this is where the scene view is created on it.
	 *
	 * The split is the context boundary: everything on the module's context is here,
	 * everything on the widget's is there. That is what keeps the render module free of the
	 * window toolkit - a second window, a headless run or a test only has to supply the
	 * size and the events.
	 */
	class ViewportFrame final
	{
	public:
		/** The frame that was drawn, for whoever shows it. */
		struct Result
		{
			/** The colour texture of the frame, in the module's context's share group. 0
			 * when nothing was drawn (the module is not up yet). */
			uint32_t texture = 0;
			int width = 0;
			int height = 0;
		};

		/** Creates the scene view on the render module's context, which has to exist already
		 * (see RenderModuleHost::Create) and to have had its Context built: the view's render
		 * targets are created here, with that context current.
		 *
		 * p_owner is the widget the frame is shown in: it is the connection context the
		 * scene tree's signals are received on, so they die with the widget. */
		explicit ViewportFrame(QWidget& p_owner);
		~ViewportFrame();

		ViewportFrame(const ViewportFrame&) = delete;
		ViewportFrame& operator=(const ViewportFrame&) = delete;

		/** The size the viewport is laid out at, in logical pixels - the widget's size. The
		 * view renders at it and the window scales the frame to its framebuffer, which is
		 * what the picking and the ImGui overlays already assume about cursor positions. */
		void SetSurfaceSize(int p_width, int p_height);

		/** Hands one Qt event to the view, and to the tool that keeps its own event
		 * handling (OCCT's interactor). */
		void HandleEvent(QEvent* p_event);

		/** Drops what the view shows about the cursor: called when the cursor leaves the
		 * viewport, which otherwise leaves the last hover highlighted. */
		void ClearHover();

		/** Renders one frame, on the module's context. */
		Result Render();

	private:
		Editor::Panels::SceneView* View() const;
		DocumentSession* Session();		/** Reads a document that was asked for, if there is one, and asks for what a read
		 * needs afterwards: the query and the fit (see SceneView::RequestFitWhenReady) and
		 * the tree. */
		void FlushDocumentRead();
		/** Reads the render pass settings panel once there is a view for it to read. */
		void ReadPassSettingsOnce();
		/** Brings the view to the size the widget has now: the view is created after the
		 * widget has been laid out, and the layout can also resize it while a frame is in
		 * flight, so the frame is what keeps the two in step rather than trusting the
		 * resize events to have reached it. */
		void SyncViewSize();
		/** The scene tree is what selects into the view: an item picked on the left is what
		 * the gizmo acts on, and hovering one is what the view highlights. */
		void ConnectTreeSelection(QWidget& p_owner);
		void OnActorSelected(Core::ECS::Actor* p_actor);
		void OnActorHovered(Core::ECS::Actor* p_actor);
		void OnActorHoverLeaved(Core::ECS::Actor* p_actor);

		Editor::Panels::SceneView* m_view = nullptr;
		std::unique_ptr<ViewportOverlay> m_overlay;
		DocumentSession* m_session = nullptr;
		int m_width = 1;
		int m_height = 1;
		bool m_passSettingsRead = false;
		/** Set by the frame a document was read in, so the frames right after it are traced:
		 * a crash a few frames later says the scene or the topology actors were not ready
		 * for what was read, and the counter says which frame it happened on. */
		bool m_traceNextFrame = false;
		/** Set when the frame's document read rebuilt geometry: the tree is a Qt model, so its
		 * rebuild is deferred to the GUI thread, right after the render job finished. */
		bool m_treeRefreshPending = false;
		/** Whether the gizmo's geometry has been built (see ViewportFrame::Render). */
		bool m_gizmoReady = false;
	};
}
