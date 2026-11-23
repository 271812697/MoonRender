#pragma once
#include <Rendering/Overlay/Interactive/EventObject.h>
#include<algorithm>

namespace MOON {
	class RenderWindowInteractor;

	class  GizmoEvent : public EventObject
	{
	public:

		static GizmoEvent* New();


		enum EventModifiers
		{
			AnyModifier = -1,
			NoModifier = 0,
			ShiftModifier = 1,
			ControlModifier = 2,
			AltModifier = 4
		};


		virtual void SetEventId(unsigned long _arg)
		{

			if (this->EventId != _arg)
			{
				this->EventId = _arg;
			}
		};
		virtual unsigned long GetEventId()
		{

			return this->EventId;
		};

		virtual void SetModifier(int _arg)
		{

			if (this->Modifier != _arg)
			{
				this->Modifier = _arg;
			}
		};
		virtual int GetModifier()
		{
			return this->Modifier;
		};

		virtual void SetKeyCode(char _arg)
		{

			if (this->KeyCode != _arg)
			{
				this->KeyCode = _arg;
			}
		};
		virtual char GetKeyCode()
		{
			return this->KeyCode;
		};

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

		static int GetModifier(RenderWindowInteractor*);

		bool operator==(GizmoEvent*) const;
		bool operator==(unsigned long GIZMOEvent) const; // event with no modifiers

	public:
		GizmoEvent();
		~GizmoEvent() override;

		unsigned long EventId;
		int Modifier;
		char KeyCode;
		int RepeatCount;
		char* KeySym;

	private:
		GizmoEvent(const GizmoEvent&) = delete;
		void operator=(const GizmoEvent&) = delete;
	};
}