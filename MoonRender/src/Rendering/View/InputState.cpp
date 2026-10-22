#include <Rendering/View/InputState.h>
namespace Editor::Panels {

	Editor::Panels::InputState::InputState()
	{
	}
	KeyState Editor::Panels::InputState::GetKeyState(KeyBoard p_key)
	{
		return m_keyEvents[p_key];
	}

	MouseButtonState InputState::GetMouseButtonState(MouseButton p_button)
	{
		return m_mouseButtonEvents[p_button];
	}

	bool InputState::IsKeyPressed(KeyBoard p_key)
	{
		return m_preKeyEvents[p_key] == KeyState::Up && m_keyEvents.at(p_key) == KeyState::Down;
	}

	bool InputState::IsKeyReleased(KeyBoard p_key)
	{
		return m_preKeyEvents[p_key] == KeyState::Down && m_keyEvents.at(p_key) == KeyState::Up;
	}

	bool InputState::IsKeyDown(KeyBoard p_key)
	{
		return m_keyEvents.find(p_key) != m_keyEvents.end() && m_keyEvents.at(p_key) == KeyState::Down;
		
	}

	bool InputState::IsKeyUp(KeyBoard p_key)
	{
		return m_keyEvents.find(p_key) != m_keyEvents.end() && m_keyEvents.at(p_key) == KeyState::Up;
	}

	bool InputState::IsMouseButtonPressed(MouseButton p_button)
	{
		return m_preMouseButtonEvents[p_button] == MouseButtonState::MOUSE_UP && m_mouseButtonEvents[p_button] == MouseButtonState::MOUSE_DOWN;
	}

	bool InputState::IsMouseButtonReleased(MouseButton p_button)
	{
		return m_preMouseButtonEvents[p_button] == MouseButtonState::MOUSE_DOWN && m_mouseButtonEvents[p_button] == MouseButtonState::MOUSE_UP;
	}

	bool InputState::IsMouseButtonDown(MouseButton p_button)
	{
		return m_mouseButtonEvents.find(p_button) != m_mouseButtonEvents.end() && m_mouseButtonEvents.at(p_button) == MouseButtonState::MOUSE_DOWN;
	}

	bool InputState::IsMouseButtonUp(MouseButton p_button)
	{
	    return m_mouseButtonEvents.find(p_button) != m_mouseButtonEvents.end() && m_mouseButtonEvents.at(p_button) == MouseButtonState::MOUSE_UP;
	}

	std::pair<double, double> InputState::GetMousePosition()
	{
		return std::pair<double, double>(mouseX, mouseY);
	}

	std::pair<double, double> InputState::GetMouseScroll()
	{
		return m_scrollData;
	}

	void InputState::ClearEvents()
	{
		m_preKeyEvents = m_keyEvents;
		m_preMouseButtonEvents = m_mouseButtonEvents;
		//m_keyEvents.clear();
		//m_mouseButtonEvents.clear();
		m_scrollData = { 0.0, 0.0 };
	}

	void InputState::ReceiveEvent(const InputEvent& p_event)
	{
		// No window toolkit here: the event already carries the engine's own keys and buttons,
		// translated by the window layer at its edge (see InputEvent).
		switch (p_event.type)
		{
		case InputEvent::EType::KeyPress:
			if (!p_event.autoRepeat)
			{
				m_keyEvents[p_event.key] = KeyState::Down;
			}
			break;

		case InputEvent::EType::KeyRelease:
			if (!p_event.autoRepeat)
			{
				m_keyEvents[p_event.key] = KeyState::Up;
			}
			break;

		case InputEvent::EType::MouseMove:
			mouseX = p_event.x;
			mouseY = p_event.y;
			break;

		case InputEvent::EType::MouseButtonPress:
			// A widget does not receive mouse-move events while no button is pressed (unless
			// it enables tracking), so moving the cursor to a new place and pressing must not
			// leave the position at the previous release: the camera would treat the whole
			// gap as the first drag delta and snap.
			mouseX = p_event.x;
			mouseY = p_event.y;
			m_mouseButtonEvents[p_event.button] = MouseButtonState::MOUSE_DOWN;
			break;

		case InputEvent::EType::MouseButtonRelease:
			mouseX = p_event.x;
			mouseY = p_event.y;
			m_mouseButtonEvents[p_event.button] = MouseButtonState::MOUSE_UP;
			break;

		case InputEvent::EType::Wheel:
			// Kept in the same units as before: the window reports angle deltas, the camera
			// controller turns them into zoom.
			m_scrollData.second = 0.002 * static_cast<float>(p_event.wheelDelta);
			break;

		default:
			break;
		}
	}
}



