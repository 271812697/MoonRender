#pragma once
#include "EventObject.h"
#include<algorithm>
class QEvent;
namespace MOON {

	class  ExecuteCommand;
	class  RenderWindowInteractor : public EventObject
	{
	public:
		static RenderWindowInteractor* New();
		static RenderWindowInteractor* Instance();
		void ReceiveEvent(QEvent* e);
		virtual void Initialize();
		void Terminate();
		void UpdateSize(int x,int y);
		void ReInitialize()
		{
			this->Initialized = 0;
			this->Enabled = 0;
			this->Initialize();
		}

		virtual void Start();
		virtual void ProcessEvents() {}
		virtual bool GetDone()
		{
			return this->Done;
		}
		void SetDone(bool flag) {
			this->Done = flag;
		}
		virtual void Enable()
		{
			this->Enabled = 1;
		}
		virtual void Disable()
		{
			this->Enabled = 0;
		}
		virtual int GetEnabled()
		{

			return this->Enabled;
		}
		virtual void SetKeyCode(char _arg)
		{
			if (this->KeyCode != _arg)
			{
				this->KeyCode = _arg;
			}
		}
		virtual char GetKeyCode()
		{

			return this->KeyCode;
		}
		virtual void SetRepeatCount(int _arg)
		{
			if (this->RepeatCount != _arg)
			{
				this->RepeatCount = _arg;
			}
		};
		virtual int GetRepeatCount()
		{

			return this->RepeatCount;
		};
		virtual void SetKeySym(const char* _arg)
		{

			if (this->KeySym == nullptr && _arg == nullptr)
			{
				return;
			}
			if (this->KeySym && _arg && (!strcmp(this->KeySym, _arg)))
			{
				return;
			}
			delete[] this->KeySym;
			if (_arg)
			{
				size_t n = strlen(_arg) + 1;
				char* cp1 = new char[n];
				const char* cp2 = (_arg);
				this->KeySym = cp1;
				do
				{
					*cp1++ = *cp2++;
				} while (--n);
			}
			else
			{
				this->KeySym = nullptr;
			}

		};
		virtual char* GetKeySym()
		{
			return this->KeySym;
		};
		virtual void TerminateApp() { this->Done = true; }
		virtual void ExitCallback();
		virtual void UserCallback();
		virtual void StartPickCallback();
		virtual void EndPickCallback();

		virtual void GetMousePosition(int* x, int* y)
		{
			*x = 0;
			*y = 0;
		}

		virtual void SetEventPosition(int x, int y)
		{

			if (this->EventPosition[0] != x || this->EventPosition[1] != y ||
				this->LastEventPosition[0] != x || this->LastEventPosition[1] != y)
			{
				this->LastEventPosition[0] = this->EventPosition[0];
				this->LastEventPosition[1] = this->EventPosition[1];
				this->EventPosition[0] = x;
				this->EventPosition[1] = y;

			}
		}
		int* GetEventPosition() {
			return this->EventPosition;
		}
		/** Position of the last event with the origin at the top-left corner.
		 *
		 * GetEventPosition() keeps the VTK convention (origin bottom-left),
		 * while the ImGui overlay and the ScreenWidget layouts work in
		 * top-left pixels, so 2D widgets read the cursor from here.
		 */
		int* GetEventPositionFlipY()
		{
			this->FlipYPosition[0] = this->EventPosition[0];
			this->FlipYPosition[1] = this->Size[1] - 1 - this->EventPosition[1];
			return this->FlipYPosition;
		}
		/** Position of the previous event, same space as GetEventPosition(). */
		int* GetLastEventPosition() {
			return this->LastEventPosition;
		}
		/** Viewport size the interactor was sized to, logical pixels.
		 *
		 * Layout and the flip in GetEventPositionFlipY() must use the same size,
		 * otherwise the cursor and the widgets end up in different spaces.
		 */
		void GetSize(int p_out[2]) const
		{
			p_out[0] = this->Size[0];
			p_out[1] = this->Size[1];
		}
		/** True while the cursor is inside the viewport (Enter/Leave events).*/
		bool IsCursorInsideViewport() const { return this->CursorInsideViewport; }
		void SetEventInformationFlipY(int x, int y, int ctrl = 0, int shift = 0, char keycode = 0,
			int repeatcount = 0, const char* keysym = nullptr)
		{
			this->SetEventInformation(
				x, this->Size[1] - y - 1, ctrl, shift, keycode, repeatcount, keysym);
		}
		virtual void SetEventPosition(int pos[2]) { this->SetEventPosition(pos[0], pos[1]); }
		virtual void SetEventPositionFlipY(int x, int y)
		{
			this->SetEventPosition(x, this->Size[1] - y - 1);
		}
		virtual void SetEventPositionFlipY(int pos[2]) { this->SetEventPositionFlipY(pos[0], pos[1]); }
		void SetEventInformation(int x, int y, int ctrl, int shift, char keycode, int repeatcount,
			const char* keysym)
		{
			this->SetEventPosition(x, y);
			this->ControlKey = ctrl;
			this->ShiftKey = shift;
			this->KeyCode = keycode;
			this->RepeatCount = repeatcount;
			if (keysym)
			{
				this->SetKeySym(keysym);
			}
		}
		void SetKeyEventInformation(int ctrl = 0, int shift = 0, char keycode = 0, int repeatcount = 0,
			const char* keysym = nullptr)
		{
			this->ControlKey = ctrl;
			this->ShiftKey = shift;
			this->KeyCode = keycode;
			this->RepeatCount = repeatcount;
			if (keysym)
			{
				this->SetKeySym(keysym);
			}
		}
		virtual void SetAltKey(int _arg)
		{

			if (this->AltKey != _arg)
			{
				this->AltKey = _arg;

			}
		}
		virtual void SetControlKey(int _arg)
		{

			if (this->ControlKey != _arg)
			{
				this->ControlKey = _arg;

			}
		}
		virtual int GetControlKey()
		{

			return this->ControlKey;
		}
		virtual int GetAltKey()
		{
			return this->AltKey;
		}
		virtual void SetShiftKey(int _arg)
		{

			if (this->ShiftKey != _arg)
			{
				this->ShiftKey = _arg;
			}
		}
		virtual int GetShiftKey()
		{

			return this->ShiftKey;
		}
		virtual void MouseMoveEvent();
		virtual void RightButtonPressEvent();
		virtual void RightButtonReleaseEvent();
		virtual void LeftButtonPressEvent();
		virtual void LeftButtonReleaseEvent();
		virtual void MiddleButtonPressEvent();
		virtual void MiddleButtonReleaseEvent();
		virtual void MouseWheelForwardEvent();
		virtual void MouseWheelBackwardEvent();
		virtual void MouseWheelLeftEvent();
		virtual void MouseWheelRightEvent();

		virtual void EnterEvent();
		virtual void LeaveEvent();
		virtual void KeyPressEvent();
		virtual void KeyReleaseEvent();
		static bool InteractorManagesTheEventLoop;
	protected:
		RenderWindowInteractor();
		~RenderWindowInteractor();

		bool Done;
		int Initialized;
		int Enabled;
		bool EnableRender;
		int Style;
		int ActorMode;
		double DesiredUpdateRate;
		double StillUpdateRate;

		// Event information
		int AltKey;
		int ControlKey;
		int ShiftKey;
		char KeyCode;
		double Rotation;
		double LastRotation;
		double Scale;
		double LastScale;
		double Translation[2];
		double LastTranslation[2];
		int RepeatCount;
		char* KeySym;
		int EventPosition[2];
		int LastEventPosition[2];
		/** Scratch for GetEventPositionFlipY() so the getter stays const-free. */
		int FlipYPosition[2];
		/** Maintained from the Enter/Leave events of the viewport widget.
		 * Starts true so a widget that is already under the cursor can react
		 * before the first Enter arrives; a false hover is harmless, a
		 * permanently false one would make the overlays unclickable. */
		bool CursorInsideViewport = true;
		int EventSize[2];
		int Size[2];
		int AccumulatedDelta=0;
		friend class InteractorObserver;
		void GrabFocus(ExecuteCommand* mouseEvents, ExecuteCommand* keypressEvents = nullptr)
		{
			this->EventObject::InternalGrabFocus(mouseEvents, keypressEvents);
		}
		void ReleaseFocus() { this->EventObject::InternalReleaseFocus(); }
		int HandleEventLoop;
		virtual void StartEventLoop() {}
	private:
		RenderWindowInteractor(const RenderWindowInteractor&) = delete;
		void operator=(const RenderWindowInteractor&) = delete;
	};

}
