#include <Rendering/Overlay/Interactive/CallbackCommand.h>


namespace MOON {
	CallbackCommand::CallbackCommand()
	{
		this->ClientData = nullptr;
		this->Callback = nullptr;
		this->ClientDataDeleteCallback = nullptr;
		this->AbortFlagOnExecute = 0;
	}

	CallbackCommand::~CallbackCommand()
	{
		if (this->ClientDataDeleteCallback)
		{
			this->ClientDataDeleteCallback(this->ClientData);
		}
	}

	//----------------------------------------------------------------
	void CallbackCommand::Execute(EventObject* caller, unsigned long event, void* callData)
	{
		if (this->Callback)
		{
			this->Callback(caller, event, this->ClientData, callData);
			if (this->AbortFlagOnExecute)
			{
				this->AbortFlagOn();
			}
		}
	}

}
