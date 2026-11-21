#pragma once
#include <Rendering/Overlay/Interactive/EventObject.h>

namespace MOON {

	class  RenderWindowInteractor;
	class  CallbackCommand;
	class  InteractorObserver : public EventObject
	{
	public:
		virtual void SetInteractor(RenderWindowInteractor* iren);
		virtual RenderWindowInteractor* GetInteractor()
		{

			return this->Interactor;
		}
		virtual void SetEnabled(int) {}
		int GetEnabled() { return this->Enabled; }
		void EnabledOn() { this->SetEnabled(1); }
		void EnabledOff() { this->SetEnabled(0); }
		void On() { this->SetEnabled(1); }
		void Off() { this->SetEnabled(0); }
		virtual void SetPriority(float _arg)
		{

			if (this->Priority != (_arg < 0.0f ? 0.0f : (_arg > 1.0f ? 1.0f : _arg)))
			{
				this->Priority = (_arg < 0.0f ? 0.0f : (_arg > 1.0f ? 1.0f : _arg));

			}
		}
		virtual float GetPriority()
		{

			return this->Priority;
		}

		virtual void SetKeyPressActivation(bool _arg)
		{

			if (this->KeyPressActivation != _arg)
			{
				this->KeyPressActivation = _arg;

			}
		}
		virtual bool GetKeyPressActivation()
		{

			return this->KeyPressActivation;
		}
		virtual void KeyPressActivationOn() { this->SetKeyPressActivation(true); }
		virtual void KeyPressActivationOff()
		{
			this->SetKeyPressActivation(false);
		}

		virtual void SetKeyPressActivationValue(char _arg)
		{

			if (this->KeyPressActivationValue != _arg)
			{
				this->KeyPressActivationValue = _arg;
			}
		}
		virtual char GetKeyPressActivationValue()
		{

			return this->KeyPressActivationValue;
		}
		virtual void OnChar();
		void GrabFocus(ExecuteCommand* mouseEvents, ExecuteCommand* keypressEvents = nullptr);
		void ReleaseFocus();

	protected:
		InteractorObserver();
		~InteractorObserver()override;

		virtual void StartInteraction();
		virtual void EndInteraction();
		static void ProcessEvents(
			EventObject* object, unsigned long event, void* clientdata, void* calldata);
		void ComputeDisplayToWorld(double x, double y, double z, double worldPt[4]);
		void ComputeWorldToDisplay(double x, double y, double z, double displayPt[3]);

		int Enabled;

		// Used to process events
		CallbackCommand* EventCallbackCommand;    // subclasses use one
		CallbackCommand* KeyPressCallbackCommand; // listens to key activation

		// Priority at which events are processed
		float Priority;
		// Keypress activation controls
		bool KeyPressActivation;
		char KeyPressActivationValue;
		// Used to associate observers with the interactor
		RenderWindowInteractor* Interactor;
		unsigned long CharObserverTag;
		unsigned long DeleteObserverTag;

	private:
		InteractorObserver(const InteractorObserver&) = delete;
		void operator=(const InteractorObserver&) = delete;
	};

}