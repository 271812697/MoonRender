#pragma once
#include <Rendering/Overlay/Interactive/ExecuteCommand.h>

namespace MOON {

	class  CallbackCommand : public ExecuteCommand
	{
	public:
		static CallbackCommand* New() { return new CallbackCommand; }
		void Execute(EventObject* caller, unsigned long eid, void* callData) override;

		virtual void SetClientData(void* cd) { this->ClientData = cd; }
		virtual void* GetClientData() { return this->ClientData; }
		virtual void SetCallback(
			void (*f)(EventObject* caller, unsigned long eid, void* clientdata, void* calldata))
		{
			this->Callback = f;
		}
		virtual void SetClientDataDeleteCallback(void (*f)(void*)) { this->ClientDataDeleteCallback = f; }


		void SetAbortFlagOnExecute(int f) { this->AbortFlagOnExecute = f; }
		int GetAbortFlagOnExecute() { return this->AbortFlagOnExecute; }
		void AbortFlagOnExecuteOn() { this->SetAbortFlagOnExecute(1); }
		void AbortFlagOnExecuteOff() { this->SetAbortFlagOnExecute(0); }

		void (*Callback)(EventObject*, unsigned long, void*, void*);
		void (*ClientDataDeleteCallback)(void*);
		~CallbackCommand() override;
	protected:

		int AbortFlagOnExecute;
		void* ClientData;
		CallbackCommand();
		
	};
}