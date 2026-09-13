#pragma once
#include <vector>

namespace MOON
{
	class ScreenWidget;

	/** Central owner list for the 2D overlay widgets.
	 *
	 * The scene picking and the camera read the input state directly instead of
	 * going through the widget event system, so "this click belongs to a 2D
	 * widget" cannot be expressed by consuming an event. Instead the widgets
	 * publish themselves here and the rest of the engine asks these questions,
	 * which also means adding a new screen widget never requires touching
	 * SceneView or the renderer again.
	 *
	 * The queries are answered on demand - no per frame snapshot - so they are
	 * correct even before the widgets have been updated in a frame, which
	 * matters because CameraController runs before the widgets do.
	 */
	class ScreenOverlayRegistry
	{
	public:
		static ScreenOverlayRegistry& Instance();

		void Register(ScreenWidget* p_widget);
		void Unregister(ScreenWidget* p_widget);

		/** Some 2D widget owns this cursor position: scene picking and the
		 * navigation cube must not react to the click. */
		bool BlocksSceneCursor(float p_x, float p_y) const;
		/** The cursor is on a clickable shape of a 2D widget. */
		bool HitsShape(float p_x, float p_y) const;
		/** A 2D widget is dragging with a captured cursor: camera orbit and
		 * panning must not start. */
		bool IsCapturing() const;
		/** Top most widget owning the position, nullptr when none does. */
		ScreenWidget* GetOwnerAt(float p_x, float p_y) const;

	private:
		ScreenOverlayRegistry() = default;
		~ScreenOverlayRegistry() = default;
		ScreenOverlayRegistry(const ScreenOverlayRegistry&) = delete;
		ScreenOverlayRegistry& operator=(const ScreenOverlayRegistry&) = delete;

	private:
		std::vector<ScreenWidget*> mWidgets;
	};
}
