#pragma once

#include <chrono>

namespace Editor::Panels {
	class SceneView;
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

		/** Draws the overlay the debug settings ask for and closes the frames. */
		void EndFrame(Editor::Panels::SceneView& p_view);

		/** TEMPORARY PROBE for the "imported STEP has no section" investigation: with
		 * MOON_CLIP_DEMO=<ratio> set, cut the framed document with a horizontal plane
		 * `<ratio>` of its height above the centre, so a headless run (Moon.exe file.step
		 * plus MOON_SHOT) renders the section without anyone dragging the clip widget.
		 * Remove together with the MOON_SECTION_DEBUG switch in SceneRenderer.cpp. */
		static void ApplyClipDemo(Editor::Panels::SceneView& p_view);

	private:
		void DrawGbufferViews(Editor::Panels::SceneView& p_view);
		void DrawFrameStats(Editor::Panels::SceneView& p_view);

		std::chrono::steady_clock::time_point m_lastFrame{};
		bool m_hasLastFrame = false;
		double m_fps = 0.0;
		double m_frameMs = 0.0;
		/** TEMPORARY: set once ApplyClipDemo cut the document (MOON_CLIP_DEMO). */
		static bool s_clipDemoApplied;
	};
}
