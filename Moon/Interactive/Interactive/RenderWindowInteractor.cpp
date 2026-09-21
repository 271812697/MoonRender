#include "RenderWindowInteractor.h"
#include "ExecuteCommand.h" 
#include "core/log.h"
#include <QMouseEvent>
#include <map>
#include <mutex>

namespace MOON {
bool RenderWindowInteractor::InteractorManagesTheEventLoop = true;
static std::mutex mu;
static const char* AsciiToKeySymTable[] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
  nullptr, nullptr, nullptr, "Tab", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
  nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
  nullptr, nullptr, nullptr, nullptr, "space", "exclam", "quotedbl", "numbersign", "dollar",
  "percent", "ampersand", "quoteright", "parenleft", "parenright", "asterisk", "plus", "comma",
  "minus", "period", "slash", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "colon",
  "semicolon", "less", "equal", "greater", "question", "at", "A", "B", "C", "D", "E", "F", "G", "H",
  "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z",
  "bracketleft", "backslash", "bracketright", "asciicircum", "underscore", "quoteleft", "a", "b",
  "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m", "n", "o", "p", "q", "r", "s", "t", "u",
  "v", "w", "x", "y", "z", "braceleft", "bar", "braceright", "asciitilde", "Delete", nullptr,
  nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
  nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
  nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
  nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
  nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
  nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
  nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
  nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
  nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
  nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
  nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
  nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
const char* ascii_to_key_sym(int i)
{
	if (i >= 0)
	{
		return AsciiToKeySymTable[i];
	}
	return nullptr;
}
const char* qt_key_to_key_sym(Qt::Key i, Qt::KeyboardModifiers modifiers)
{
	const char* ret = nullptr;
	switch (i)
	{
			// Cancel
		case Qt::Key_Backspace: ret = "BackSpace"; break;
		case Qt::Key_Tab: ret = "Tab"; break;
		case Qt::Key_Backtab: ret = "Tab"; break;
		case Qt::Key_Clear: ret = "Clear"; break;
		case Qt::Key_Return: ret = "Return"; break;
		case Qt::Key_Enter: ret = "Return"; break;
		case Qt::Key_Shift: ret = "Shift_L"; break;
		case Qt::Key_Control: ret = "Control_L"; break;
		case Qt::Key_Alt: ret = "Alt_L"; break;
		case Qt::Key_Pause: ret = "Pause"; break;
		case Qt::Key_CapsLock: ret = "Caps_Lock"; break;
		case Qt::Key_Escape: ret = "Escape"; break;
		case Qt::Key_Space: ret = "space"; break;
		case Qt::Key_PageUp: ret = "Prior"; break;
		case Qt::Key_PageDown: ret = "Next"; break;
		case Qt::Key_End: ret = "End"; break;
		case Qt::Key_Home: ret = "Home"; break;
		case Qt::Key_Left: ret = "Left"; break;
		case Qt::Key_Up: ret = "Up"; break;
		case Qt::Key_Right: ret = "Right"; break;
		case Qt::Key_Down: ret = "Down"; break;
		case Qt::Key_Select: ret = "Select"; break;
		case Qt::Key_Execute: ret = "Execute"; break;
		case Qt::Key_SysReq: ret = "Snapshot"; break;
		case Qt::Key_Insert: ret = "Insert"; break;
		case Qt::Key_Delete: ret = "Delete"; break;
		case Qt::Key_Help: ret = "Help"; break;
		case Qt::Key_0: ret = (modifiers & Qt::KeypadModifier) ? ("KP_0") : ("0"); break;
		case Qt::Key_1: ret = (modifiers & Qt::KeypadModifier) ? ("KP_1") : ("1"); break;
		case Qt::Key_2: ret = (modifiers & Qt::KeypadModifier) ? ("KP_2") : ("2"); break;
		case Qt::Key_3: ret = (modifiers & Qt::KeypadModifier) ? ("KP_3") : ("3"); break;
		case Qt::Key_4: ret = (modifiers & Qt::KeypadModifier) ? ("KP_4") : ("4"); break;
		case Qt::Key_5: ret = (modifiers & Qt::KeypadModifier) ? ("KP_5") : ("5"); break;
		case Qt::Key_6: ret = (modifiers & Qt::KeypadModifier) ? ("KP_6") : ("6"); break;
		case Qt::Key_7: ret = (modifiers & Qt::KeypadModifier) ? ("KP_7") : ("7"); break;
		case Qt::Key_8: ret = (modifiers & Qt::KeypadModifier) ? ("KP_8") : ("8"); break;
		case Qt::Key_9: ret = (modifiers & Qt::KeypadModifier) ? ("KP_9") : ("9"); break;
		case Qt::Key_A: ret = "a"; break;
		case Qt::Key_B: ret = "b"; break;
		case Qt::Key_C: ret = "c"; break;
		case Qt::Key_D: ret = "d"; break;
		case Qt::Key_E: ret = "e"; break;
		case Qt::Key_F: ret = "f"; break;
		case Qt::Key_G: ret = "g"; break;
		case Qt::Key_H: ret = "h"; break;
		case Qt::Key_I: ret = "i"; break;
		case Qt::Key_J: ret = "h"; break;
		case Qt::Key_K: ret = "k"; break;
		case Qt::Key_L: ret = "l"; break;
		case Qt::Key_M: ret = "m"; break;
		case Qt::Key_N: ret = "n"; break;
		case Qt::Key_O: ret = "o"; break;
		case Qt::Key_P: ret = "p"; break;
		case Qt::Key_Q: ret = "q"; break;
		case Qt::Key_R: ret = "r"; break;
		case Qt::Key_S: ret = "s"; break;
		case Qt::Key_T: ret = "t"; break;
		case Qt::Key_U: ret = "u"; break;
		case Qt::Key_V: ret = "v"; break;
		case Qt::Key_W: ret = "w"; break;
		case Qt::Key_X: ret = "x"; break;
		case Qt::Key_Y: ret = "y"; break;
		case Qt::Key_Z: ret = "z"; break;
		case Qt::Key_Asterisk: ret = "asterisk"; break;
		case Qt::Key_Plus: ret = "plus"; break;
		case Qt::Key_Bar: ret = "bar"; break;
		case Qt::Key_Minus: ret = "minus"; break;
		case Qt::Key_Period: ret = "period"; break;
		case Qt::Key_Slash: ret = "slash"; break;
		case Qt::Key_F1: ret = "F1"; break;
		case Qt::Key_F2: ret = "F2"; break;
		case Qt::Key_F3: ret = "F3"; break;
		case Qt::Key_F4: ret = "F4"; break;
		case Qt::Key_F5: ret = "F5"; break;
		case Qt::Key_F6: ret = "F6"; break;
		case Qt::Key_F7: ret = "F7"; break;
		case Qt::Key_F8: ret = "F8"; break;
		case Qt::Key_F9: ret = "F9"; break;
		case Qt::Key_F10: ret = "F10"; break;
		case Qt::Key_F11: ret = "F11"; break;
		case Qt::Key_F12: ret = "F12"; break;
		case Qt::Key_F13: ret = "F13"; break;
		case Qt::Key_F14: ret = "F14"; break;
		case Qt::Key_F15: ret = "F15"; break;
		case Qt::Key_F16: ret = "F16"; break;
		case Qt::Key_F17: ret = "F17"; break;
		case Qt::Key_F18: ret = "F18"; break;
		case Qt::Key_F19: ret = "F19"; break;
		case Qt::Key_F20: ret = "F20"; break;
		case Qt::Key_F21: ret = "F21"; break;
		case Qt::Key_F22: ret = "F22"; break;
		case Qt::Key_F23: ret = "F23"; break;
		case Qt::Key_F24: ret = "F24"; break;
		case Qt::Key_NumLock: ret = "Num_Lock"; break;
		case Qt::Key_ScrollLock: ret = "Scroll_Lock"; break;
		default:
			break;
	}
	return ret;
}
RenderWindowInteractor::RenderWindowInteractor()
{
 
  this->Initialized = 0;
  this->Enabled = 0;
  this->EnableRender = true;
  this->DesiredUpdateRate = 15;

  this->StillUpdateRate = 0.0001;


  this->EventPosition[0] = this->LastEventPosition[0] = 0;
  this->EventPosition[1] = this->LastEventPosition[1] = 0;
  this->FlipYPosition[0] = 0;
  this->FlipYPosition[1] = 0;
  this->CursorInsideViewport = true;



  this->EventSize[0] = 0;
  this->EventSize[1] = 0;

  this->Size[0] = 0;
  this->Size[1] = 0;

  this->Translation[0] = this->LastTranslation[0] = 0;
  this->Translation[1] = this->LastTranslation[1] = 0;


  this->AltKey = 0;
  this->ControlKey = 0;
  this->ShiftKey = 0;
  this->KeyCode = 0;
  this->Rotation = 0;
  this->LastRotation = 0;
  this->Scale = 0;
  this->LastScale = 0;
  this->RepeatCount = 0;
  this->KeySym = nullptr;
  this->HandleEventLoop = false;
  this->Done = false;
}

//------------------------------------------------------------------------------
RenderWindowInteractor::~RenderWindowInteractor()
{

  delete[] this->KeySym;
 
}
//------------------------------------------------------------------------------
void RenderWindowInteractor::Start()
{

  if (this->HasObserver(ExecuteCommand::StartEvent) && !this->HandleEventLoop)
  {
    this->InvokeEvent(ExecuteCommand::StartEvent, nullptr);
    return;
  }

  if (!this->Initialized)
  {
    this->Initialize();

    if (!this->Initialized)
    {
      return;
    }
  }

  this->Done = false;
  this->StartEventLoop();
}




//------------------------------------------------------------------------------
void RenderWindowInteractor::ExitCallback()
{
  if (this->HasObserver(ExecuteCommand::ExitEvent))
  {
    this->InvokeEvent(ExecuteCommand::ExitEvent, nullptr);
  }
  else
  {
    this->TerminateApp();
  }
}

//------------------------------------------------------------------------------
void RenderWindowInteractor::UserCallback()
{
  this->InvokeEvent(ExecuteCommand::UserEvent, nullptr);
}

//------------------------------------------------------------------------------
void RenderWindowInteractor::StartPickCallback()
{
  this->InvokeEvent(ExecuteCommand::StartPickEvent, nullptr);
}

//------------------------------------------------------------------------------
void RenderWindowInteractor::EndPickCallback()
{
  this->InvokeEvent(ExecuteCommand::EndPickEvent, nullptr);
}
RenderWindowInteractor* RenderWindowInteractor::New()
{
    return new RenderWindowInteractor();
}
RenderWindowInteractor* RenderWindowInteractor::Instance() {
    static RenderWindowInteractor* interactor = nullptr;
    if (interactor == nullptr) {
        std::unique_lock<std::mutex> lock(mu);
        if (interactor ==nullptr) {
            interactor = new RenderWindowInteractor();
            interactor->Initialize();
        }
    }
    return interactor;
}
void RenderWindowInteractor::ReceiveEvent(QEvent*e) {
	if (e == nullptr)
		return ;

	const QEvent::Type t = e->type();

	if (!GetEnabled())
		return ;

	if (t == QEvent::MouseButtonPress || t == QEvent::MouseButtonRelease ||
		t == QEvent::MouseButtonDblClick || t == QEvent::MouseMove || t == QEvent::HoverMove)
	{

		QMouseEvent* e2 = static_cast<QMouseEvent*>(e);
		auto x = e2->x();
		auto y = e2->y();
		SetEventInformationFlipY(
			static_cast<int>(x),
			static_cast<int>(y),
			(e2->modifiers() & Qt::ControlModifier) > 0 ? 1 : 0,
			(e2->modifiers() & Qt::ShiftModifier) > 0 ? 1 : 0, 0,
			e2->type() == QEvent::MouseButtonDblClick ? 1 : 0);
		SetAltKey((e2->modifiers() & Qt::AltModifier) > 0 ? 1 : 0);
		if (t == QEvent::MouseMove || t == QEvent::HoverMove)
		{
			InvokeEvent(ExecuteCommand::MouseMoveEvent, e2);
		}

		else if (t == QEvent::MouseButtonPress)
		{
			switch (e2->button())
			{
			case Qt::LeftButton:
				
				InvokeEvent(ExecuteCommand::LeftButtonPressEvent, e2);
				break;

			case Qt::MiddleButton:
				InvokeEvent(ExecuteCommand::MiddleButtonPressEvent, e2);
				break;

			case Qt::RightButton:
				InvokeEvent(ExecuteCommand::RightButtonPressEvent, e2);
				break;

			default:
				break;
			}
		}
		else if (t == QEvent::MouseButtonDblClick)
		{
			switch (e2->button())
			{
			case Qt::LeftButton:
				//InvokeEvent(ExecuteCommand::LeftButtonDoubleClickEvent, e2);
				break;

			case Qt::MiddleButton:
				//iren->InvokeEvent(ExecuteCommand::MiddleButtonDoubleClickEvent, e2);
				break;

			case Qt::RightButton:
				//iren->InvokeEvent(ExecuteCommand::RightButtonDoubleClickEvent, e2);
				break;

			default:
				break;
			}
		}
		else if (t == QEvent::MouseButtonRelease)
		{
			switch (e2->button())
			{
			case Qt::LeftButton:
				InvokeEvent(ExecuteCommand::LeftButtonReleaseEvent, e2);
				break;

			case Qt::MiddleButton:
				InvokeEvent(ExecuteCommand::MiddleButtonReleaseEvent, e2);
				break;

			case Qt::RightButton:
				InvokeEvent(ExecuteCommand::RightButtonReleaseEvent, e2);
				break;

			default:
				break;
			}
		}
		return;
	}


	if (t == QEvent::Enter)
	{
		this->CursorInsideViewport = true;
		InvokeEvent(ExecuteCommand::EnterEvent, e);
		return ;
	}

	if (t == QEvent::Leave)
	{
		this->CursorInsideViewport = false;
		InvokeEvent(ExecuteCommand::LeaveEvent, e);
		return ;
	}

	if (t == QEvent::KeyPress || t == QEvent::KeyRelease)
	{
		
		QKeyEvent* e2 = static_cast<QKeyEvent*>(e);

		// get key and keysym information
		int ascii_key = e2->text().length() ? e2->text().unicode()->toLatin1() : 0;
		const char* keysym = ascii_to_key_sym(ascii_key);
		if (!keysym || e2->modifiers() == Qt::KeypadModifier)
		{
			// get virtual keys
			keysym = qt_key_to_key_sym(static_cast<Qt::Key>(e2->key()), e2->modifiers());
		}

		if (!keysym)
		{
			keysym = "None";
		}

		// give interactor event information
		SetKeyEventInformation((e2->modifiers() & Qt::ControlModifier),
			(e2->modifiers() & Qt::ShiftModifier), ascii_key, e2->count(), keysym);
		SetAltKey((e2->modifiers() & Qt::AltModifier) > 0 ? 1 : 0);

		if (t == QEvent::KeyPress)
		{

			InvokeEvent(ExecuteCommand::KeyPressEvent, e2);
			if (ascii_key)
			{
				InvokeEvent(ExecuteCommand::CharEvent, e2);
			}
		}
		else
		{
			InvokeEvent(ExecuteCommand::KeyReleaseEvent, e2);
		}
		return ;
	}
	
	if (t == QEvent::Wheel)
	{
		QWheelEvent* e2 = static_cast<QWheelEvent*>(e);
#if QT_VERSION < QT_VERSION_CHECK(5, 14, 0)
		auto x = e2->x();
		auto y = e2->y();
#else
		auto x = e2->position().x();
		auto y = e2->position().y();
#endif

		SetEventInformationFlipY(
			static_cast<int>(x ),
			static_cast<int>(y ),
			(e2->modifiers() & Qt::ControlModifier) > 0 ? 1 : 0,
			(e2->modifiers() & Qt::ShiftModifier) > 0 ? 1 : 0);
		SetAltKey((e2->modifiers() & Qt::AltModifier) > 0 ? 1 : 0);

		double horizontalDelta = e2->angleDelta().x();
		double verticalDelta = e2->angleDelta().y();

		const int threshold = 120;

		// invoke vtk event when accumulated delta passes the threshold
		if (this->AccumulatedDelta >= threshold && verticalDelta != 0.0)
		{
			InvokeEvent(ExecuteCommand::MouseWheelForwardEvent, e2);
			this->AccumulatedDelta = 0;
		}
		else if (this->AccumulatedDelta <= -threshold && verticalDelta != 0.0)
		{
			InvokeEvent(ExecuteCommand::MouseWheelBackwardEvent, e2);
			this->AccumulatedDelta = 0;
		}
		else if (this->AccumulatedDelta >= threshold && horizontalDelta != 0.0)
		{
			InvokeEvent(ExecuteCommand::MouseWheelLeftEvent, e2);
			this->AccumulatedDelta = 0;
		}
		else if (this->AccumulatedDelta <= -threshold && horizontalDelta != 0.0)
		{
			InvokeEvent(ExecuteCommand::MouseWheelRightEvent, e2);
			this->AccumulatedDelta = 0;
		}
	}
	return ;
}

//------------------------------------------------------------------------------
void RenderWindowInteractor::Initialize()
{
  this->Initialized = 1;
  this->Enable();

}

void RenderWindowInteractor::Terminate()
{
    delete this;
}

void RenderWindowInteractor::UpdateSize(int x, int y)
{
	this->Size[0] = x;
	this->Size[1] = y;
}


//------------------------------------------------------------------
void RenderWindowInteractor::MouseMoveEvent()
{
  if (!this->Enabled)
  {
    return;
  }
  this->InvokeEvent(ExecuteCommand::MouseMoveEvent, nullptr);
}

//------------------------------------------------------------------
void RenderWindowInteractor::RightButtonPressEvent()
{
  if (!this->Enabled)
  {
    return;
  }
  this->InvokeEvent(ExecuteCommand::RightButtonPressEvent, nullptr);
}

//------------------------------------------------------------------
void RenderWindowInteractor::RightButtonReleaseEvent()
{
  if (!this->Enabled)
  {
    return;
  }
  this->InvokeEvent(ExecuteCommand::RightButtonReleaseEvent, nullptr);
}

//------------------------------------------------------------------
void RenderWindowInteractor::LeftButtonPressEvent()
{
  if (!this->Enabled)
  {
    return;
  }
  this->InvokeEvent(ExecuteCommand::LeftButtonPressEvent, nullptr);
}

//------------------------------------------------------------------
void RenderWindowInteractor::LeftButtonReleaseEvent()
{
  if (!this->Enabled)
  {
    return;
  }
  this->InvokeEvent(ExecuteCommand::LeftButtonReleaseEvent, nullptr);
}

//------------------------------------------------------------------
void RenderWindowInteractor::MiddleButtonPressEvent()
{
  if (!this->Enabled)
  {
    return;
  }
  this->InvokeEvent(ExecuteCommand::MiddleButtonPressEvent, nullptr);
}

//------------------------------------------------------------------
void RenderWindowInteractor::MiddleButtonReleaseEvent()
{
  if (!this->Enabled)
  {
    return;
  }
  this->InvokeEvent(ExecuteCommand::MiddleButtonReleaseEvent, nullptr);
}

//------------------------------------------------------------------
void RenderWindowInteractor::MouseWheelForwardEvent()
{
  if (!this->Enabled)
  {
    return;
  }
  this->InvokeEvent(ExecuteCommand::MouseWheelForwardEvent, nullptr);
}

//------------------------------------------------------------------
void RenderWindowInteractor::MouseWheelBackwardEvent()
{
  if (!this->Enabled)
  {
    return;
  }
  this->InvokeEvent(ExecuteCommand::MouseWheelBackwardEvent, nullptr);
}

//------------------------------------------------------------------
void RenderWindowInteractor::MouseWheelLeftEvent()
{
  if (!this->Enabled)
  {
    return;
  }
  this->InvokeEvent(ExecuteCommand::MouseWheelLeftEvent, nullptr);
}

//------------------------------------------------------------------
void RenderWindowInteractor::MouseWheelRightEvent()
{
  if (!this->Enabled)
  {
    return;
  }
  this->InvokeEvent(ExecuteCommand::MouseWheelRightEvent, nullptr);
}

void RenderWindowInteractor::EnterEvent()
{
  if (!this->Enabled)
  {
    return;
  }
  this->InvokeEvent(ExecuteCommand::EnterEvent, nullptr);
}

//------------------------------------------------------------------
void RenderWindowInteractor::LeaveEvent()
{
  if (!this->Enabled)
  {
    return;
  }
  this->InvokeEvent(ExecuteCommand::LeaveEvent, nullptr);
}

//------------------------------------------------------------------
void RenderWindowInteractor::KeyPressEvent()
{
  if (!this->Enabled)
  {
    return;
  }
  this->InvokeEvent(ExecuteCommand::KeyPressEvent, nullptr);
}

//------------------------------------------------------------------
void RenderWindowInteractor::KeyReleaseEvent()
{
  if (!this->Enabled)
  {
    return;
  }
  this->InvokeEvent(ExecuteCommand::KeyReleaseEvent, nullptr);
}
}
