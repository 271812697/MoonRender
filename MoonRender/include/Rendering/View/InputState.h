#pragma once
#include <Rendering/View/InpuKey.h>
#include <Rendering/View/InputEvent.h>
#include <unordered_map>

namespace Editor {
	namespace Panels
	{
		class InputState {
		public:
			InputState();
			KeyState GetKeyState(KeyBoard p_key);
			MouseButtonState GetMouseButtonState(MouseButton p_button);

			bool IsKeyPressed(KeyBoard p_key);
			bool IsKeyReleased(KeyBoard p_key);
			bool IsKeyDown(KeyBoard p_key);
			bool IsKeyUp(KeyBoard p_key);

			bool IsMouseButtonPressed(MouseButton p_button);
			bool IsMouseButtonReleased(MouseButton p_button);
			bool IsMouseButtonDown(MouseButton p_button);
			bool IsMouseButtonUp(MouseButton p_button);
			std::pair<double, double> GetMousePosition();

			std::pair<double, double> GetMouseScroll();

			void ClearEvents();
			/** Takes an event the window layer already translated, see InputEvent. */
			void ReceiveEvent(const InputEvent& p_event);
		private:

			std::unordered_map<KeyBoard, KeyState> m_keyEvents;
			std::unordered_map<KeyBoard, KeyState> m_preKeyEvents;
			std::unordered_map<MouseButton, MouseButtonState> m_mouseButtonEvents;
			std::unordered_map<MouseButton, MouseButtonState> m_preMouseButtonEvents;
			std::pair<double, double> m_scrollData;
			int mouseX = 0, mouseY = 0;
		};
	}
}