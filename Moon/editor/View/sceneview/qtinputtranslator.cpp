#include "qtinputtranslator.h"

#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>

namespace MOON::ViewportInput
{
	bool TranslateQtEvent(QEvent* p_event, ::Editor::Panels::InputEvent& p_out)
	{
		using ::Editor::Panels::InputEvent;
		// The engine's own key/button enums live in that namespace; the case labels below
		// are its values, not Qt's.
		using namespace ::Editor::Panels;
		switch (p_event->type())
		{
		case QEvent::KeyPress:
		case QEvent::KeyRelease:
		{
			auto* keyEvent = static_cast<QKeyEvent*>(p_event);
			p_out.type = p_event->type() == QEvent::KeyPress ? InputEvent::EType::KeyPress : InputEvent::EType::KeyRelease;
			p_out.autoRepeat = keyEvent->isAutoRepeat();
			switch (static_cast<Qt::Key>(keyEvent->key()))
			{
			case Qt::Key_W: p_out.key = KEYW; break;
			case Qt::Key_A: p_out.key = KEYA; break;
			case Qt::Key_S: p_out.key = KEYS; break;
			case Qt::Key_D: p_out.key = KEYD; break;
			case Qt::Key_Q: p_out.key = KEYQ; break;
			case Qt::Key_E: p_out.key = KEYE; break;
			case Qt::Key_R: p_out.key = KEYR; break;
			case Qt::Key_L: p_out.key = KEYL; break;
			case Qt::Key_T: p_out.key = KEYT; break;
			case Qt::Key_F: p_out.key = KEYF; break;
			case Qt::Key_Alt: p_out.key = ALTA; break;
			case Qt::Key_Right: p_out.key = RIGHT; break;
			case Qt::Key_Left: p_out.key = LEFT; break;
			case Qt::Key_Up: p_out.key = UP; break;
			case Qt::Key_Down: p_out.key = DOWN; break;
			case Qt::Key_PageUp: p_out.key = PageUp; break;
			case Qt::Key_PageDown: p_out.key = PageDown; break;
			case Qt::Key_Control: p_out.key = Control; break;
			case Qt::Key_Shift: p_out.key = Shift; break;
			default: return false;
			}
			return true;
		}
		case QEvent::MouseMove:
		{
			auto* mouseEvent = static_cast<QMouseEvent*>(p_event);
			p_out.type = InputEvent::EType::MouseMove;
			p_out.x = mouseEvent->x();
			p_out.y = mouseEvent->y();
			return true;
		}
		case QEvent::MouseButtonPress:
		case QEvent::MouseButtonRelease:
		{
			auto* mouseEvent = static_cast<QMouseEvent*>(p_event);
			p_out.type = p_event->type() == QEvent::MouseButtonPress
				? InputEvent::EType::MouseButtonPress : InputEvent::EType::MouseButtonRelease;
			switch (mouseEvent->button())
			{
			case Qt::LeftButton: p_out.button = MOUSE_BUTTON_LEFT; break;
			case Qt::RightButton: p_out.button = MOUSE_BUTTON_RIGHT; break;
			case Qt::MiddleButton: p_out.button = MOUSE_BUTTON_MIDDLE; break;
			default: return false;
			}
			p_out.x = mouseEvent->x();
			p_out.y = mouseEvent->y();
			return true;
		}
		case QEvent::Wheel:
		{
			auto* wheelEvent = static_cast<QWheelEvent*>(p_event);
			p_out.type = InputEvent::EType::Wheel;
			p_out.wheelDelta = wheelEvent->angleDelta().y();
			return true;
		}
		default:
			return false;
		}
	}
}
