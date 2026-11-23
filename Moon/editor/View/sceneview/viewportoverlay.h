#pragma once

#include <chrono>

namespace Editor::Panels {
	class SceneView;
}
namespace Rendering::Context {
	class Driver;
}

namespace MOON
{
	/** What the viewport draws *into* its frame besides the scene: the G-buffer views and
	 * the frame statistics the debug settings ask for.
	 *
	 * It is inside the frame on purpose - the overlays belong to the image the window
	 * pastes, not to something drawn on the window's context afterwards - so this is the
	 * pair of calls the frame loop makes around the draw: BeginFrame opens ImGui's frame and
	 * the 2D draw list's, EndFrame draws the overlay and closes both. Both need the render
	 * module's context current, which the frame loop has already made.
	 */
	class ViewportOverlay final
	{
	public:
		ViewportOverlay();

		/** Opens the overlay frames and measures the frame time against the previous one. */
		void BeginFrame(Editor::Panels::SceneView& p_view);

		/** Opens only the 3D overlay's frame (the gizmos, the view cube): the facts the gizmo pass
		 * draws against, and nothing from ImGui.
		 *
		 * This is what a render thread can run: the 3D overlay draws through the command stream
		 * (Rendering::Overlay::Im3DGLRenderer), so the recording side needs neither a context nor
		 * an ImGui frame. ImGui and the 2D draw list keep using BeginFrame/EndFrame until their
		 * GL is commands too. */
		void BeginGizmoFrame(Editor::Panels::SceneView& p_view);

		/** Opens the 2D draw list's frame (the widgets' icons and lines). Same story as
		 * BeginGizmoFrame: the list is built on this side and drawn through the command stream. */
		void BeginOverlay2D(Editor::Panels::SceneView& p_view);

		/** Draws the 2D draw list into the frame's target (a command), without the ImGui parts. */
		void EndOverlay2D(Editor::Panels::SceneView& p_view, ::Rendering::Context::Driver& p_driver);

		/** Draws the overlay the debug settings ask for and closes the frames. p_driver is the
		 * command recorder the 2D overlay records its state setup on. */
		void EndFrame(Editor::Panels::SceneView& p_view, ::Rendering::Context::Driver& p_driver);

		/** TEMPORARY PROBE for the "imported STEP has no section" investigation: with
		 * MOON_CLIP_DEMO=<ratio> set, cut the framed document with a horizontal plane
		 * `<ratio>` of its height above the centre, so a headless run (Moon.exe file.step
		 * plus MOON_SHOT) renders the section without anyone dragging the clip widget.
		 * Remove together with the MOON_SECTION_DEBUG switch in SceneRenderer.cpp. */
		static void ApplyClipDemo(Editor::Panels::SceneView& p_view);

	private:
		void DrawFrameStats(Editor::Panels::SceneView& p_view);

		std::chrono::steady_clock::time_point m_lastFrame{};
		bool m_hasLastFrame = false;
		double m_fps = 0.0;
		double m_frameMs = 0.0;
		/** TEMPORARY: set once ApplyClipDemo cut the document (MOON_CLIP_DEMO). */
		static bool s_clipDemoApplied;
	};
}
