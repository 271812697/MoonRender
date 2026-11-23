#include <Rendering/Overlay/Interactive/EventObserver.h>
#include <Rendering/Overlay/Interactive/CallbackCommand.h>
#include <Rendering/Overlay/Interactive/RenderWindowInteractor.h>

namespace MOON {

	InteractorObserver::InteractorObserver()
	{
		this->Enabled = 0;

		this->Interactor = nullptr;

		this->EventCallbackCommand = CallbackCommand::New();
		this->EventCallbackCommand->SetClientData(this);

		// subclass has to invoke SetCallback()

		this->KeyPressCallbackCommand = CallbackCommand::New();
		this->KeyPressCallbackCommand->SetClientData(this);
		this->KeyPressCallbackCommand->SetCallback(InteractorObserver::ProcessEvents);



		this->Priority = 0.0f;


		this->KeyPressActivation = 1;
		this->KeyPressActivationValue = 'i';

		this->CharObserverTag = 0;
		this->DeleteObserverTag = 0;
	}

	InteractorObserver::~InteractorObserver()
	{


		this->SetEnabled(0);
		this->SetInteractor(nullptr);
	}

	void InteractorObserver::SetInteractor(RenderWindowInteractor* i)
	{
		if (i == this->Interactor)
		{
			return;
		}
		// if we already have an Interactor then stop observing it
		if (this->Interactor)
		{
			this->SetEnabled(0); // disable the old interactor
			this->Interactor->RemoveObserver(this->CharObserverTag);
			this->CharObserverTag = 0;
			this->Interactor->RemoveObserver(this->DeleteObserverTag);
			this->DeleteObserverTag = 0;
		}

		this->Interactor = i;

		// add observers for each of the events handled in ProcessEvents
		if (i)
		{
			this->CharObserverTag =
				i->AddObserver(ExecuteCommand::CharEvent, this->KeyPressCallbackCommand, this->Priority);
			this->DeleteObserverTag =
				i->AddObserver(ExecuteCommand::DeleteEvent, this->KeyPressCallbackCommand, this->Priority);

		}
	}

	void InteractorObserver::ProcessEvents(
		EventObject* object, unsigned long event, void* clientdata, void* calldata)
	{
		if (event == ExecuteCommand::CharEvent || event == ExecuteCommand::DeleteEvent)
		{
			EventObject* vobj = reinterpret_cast<EventObject*>(clientdata);
			InteractorObserver* self = static_cast<InteractorObserver*>(vobj);
			if (self)
			{
				if (event == ExecuteCommand::CharEvent)
				{
					self->OnChar();
				}
				else // delete event
				{
					self->SetInteractor(nullptr);
				}
			}
			else
			{

			}
		}
	}

	void InteractorObserver::StartInteraction()
	{

	}

	//------------------------------------------------------------------------------
	void InteractorObserver::EndInteraction()
	{

	}


	void InteractorObserver::ComputeDisplayToWorld(double x, double y, double z, double worldPt[4])
	{

	}


	void InteractorObserver::ComputeWorldToDisplay(double x, double y, double z, double displayPt[3])
	{

	}

	void InteractorObserver::OnChar()
	{

		if (this->KeyPressActivation)
		{
			if (this->Interactor->GetKeyCode() == this->KeyPressActivationValue)
			{
				if (!this->Enabled)
				{
					this->On();
				}
				else
				{
					this->Off();
				}
				this->KeyPressCallbackCommand->SetAbortFlag(1);
			}
		}
	}

	void InteractorObserver::GrabFocus(ExecuteCommand* mouseEvents, ExecuteCommand* keypressEvents)
	{
		if (this->Interactor)
		{
			this->Interactor->GrabFocus(mouseEvents, keypressEvents);
		}
	}

	void InteractorObserver::ReleaseFocus()
	{
		if (this->Interactor)
		{
			this->Interactor->ReleaseFocus();
		}
	}
}
