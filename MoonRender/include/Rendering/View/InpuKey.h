#pragma once

namespace Editor{
	namespace Panels
	{
		enum KeyState
		{
			Down = 0,
			Up
		};
		enum KeyBoard
		{
			KEYW = 0,
			KEYA,
			KEYS,
			KEYD,
			KEYQ,
			KEYE,
			KEYR,
			KEYL,
			KEYT,
			KEYF,
			ALTA,
			RIGHT,
			LEFT,
			UP,
			DOWN,
			PageUp,
			PageDown,
			Control,
			Shift
		};
		enum MouseButtonState {
			MOUSE_UP = 0,
			MOUSE_DOWN
		};
		enum MouseButton {
			MOUSE_BUTTON_LEFT = 0,
			MOUSE_BUTTON_RIGHT,
			MOUSE_BUTTON_MIDDLE
		};

	}
}