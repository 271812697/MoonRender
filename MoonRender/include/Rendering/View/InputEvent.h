#pragma once
#include <Rendering/View/InpuKey.h>

namespace Editor::Panels
{
	/** One input event, in the render module's own terms.
	 *
	 * The view and its camera controller take these, not window-system events: what a window
	 * framework delivers (Qt's QEvent here, something else elsewhere) is translated by the window
	 * layer, at its edge. That is what keeps the rendering side free of the window toolkit - it
	 * only ever sees keys, buttons, positions and wheel deltas.
	 *
	 * The keys and buttons are the engine's own enums (InpuKey.h), so a translation is a switch
	 * from the window's values to these, and nothing else in the render path names Qt. */
	struct InputEvent
	{
		enum class EType
		{
			None = 0,
			KeyPress,
			KeyRelease,
			MouseMove,
			MouseButtonPress,
			MouseButtonRelease,
			Wheel
		};

		EType type = EType::None;

		/** KeyBoard value; only meaningful for the two key events. */
		KeyBoard key = KEYW;
		/** MouseButton value; only meaningful for the two button events. */
		MouseButton button = MOUSE_BUTTON_LEFT;
		/** Held keys are not re-sent while the window auto-repeats. */
		bool autoRepeat = false;

		/** Cursor position in the view, for the mouse events. */
		int x = 0;
		int y = 0;
		/** Wheel movement, in the window's units (Qt's angleDelta().y()). */
		int wheelDelta = 0;
	};
}
