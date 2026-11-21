#pragma  once
namespace MOON {
	class  EventObject;
	class  ExecuteCommand
	{
	public:

		virtual void Execute(EventObject* caller, unsigned long eventId, void* callData) = 0;
		static const char* GetStringFromEventId(unsigned long event);
		static unsigned long GetEventIdFromString(const char* event);

		static bool EventHasData(unsigned long event);


		void SetAbortFlag(bool f) { this->AbortFlag = f; }
		bool GetAbortFlag() { return this->AbortFlag; }
		void AbortFlagOn() { this->SetAbortFlag(1); }
		void AbortFlagOff() { this->SetAbortFlag(0); }


		void SetPassiveObserver(bool f) { this->PassiveObserver = f; }
		bool GetPassiveObserver() { return this->PassiveObserver; }
		void PassiveObserverOn() { this->SetPassiveObserver(true); }
		void PassiveObserverOff() { this->SetPassiveObserver(false); }
		enum EventIds
		{
			NoEvent = 0,
			AnyEvent,
			DeleteEvent,
			StartEvent,
			EndEvent,
			RenderEvent,
			PickEvent,
			StartPickEvent,
			EndPickEvent,
			LeftButtonPressEvent,
			LeftButtonReleaseEvent,
			MiddleButtonPressEvent,
			MiddleButtonReleaseEvent,
			RightButtonPressEvent,
			RightButtonReleaseEvent,
			EnterEvent,
			LeaveEvent,
			KeyPressEvent,
			KeyReleaseEvent,
			CharEvent,
			MouseMoveEvent,
			MouseWheelForwardEvent,
			MouseWheelBackwardEvent,
			MouseWheelLeftEvent,
			MouseWheelRightEvent,
			StartInteractionEvent,
			InteractionEvent,
			EndInteractionEvent,
			EnableEvent,
			DisableEvent,
			ExitEvent,
			UserEvent = 1000
		};	
		
		virtual ~ExecuteCommand() = default;

	protected:
		bool AbortFlag;
		bool PassiveObserver;
        ExecuteCommand();
	
		friend class SubjectHelper;

		ExecuteCommand(const ExecuteCommand& c) = default;
		void operator=(const ExecuteCommand&) {}
	};
}
