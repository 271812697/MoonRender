#include <Rendering/Overlay/Interactive/AbstractWidget.h>
#include <Rendering/Overlay/Interactive/CallbackCommand.h>
#include <Rendering/Overlay/Interactive/Event.h>
#include <Rendering/Overlay/Interactive/RenderWindowInteractor.h>
#include <Rendering/Overlay/Interactive/WidgetCallbackMapper.h>
#include <Rendering/Overlay/Interactive/WidgetEvent.h>
#include <Rendering/Overlay/Interactive/WidgetEventTranslator.h>
//------------------------------------------------------------------------------
namespace MOON {
	AbstractWidget::AbstractWidget()
	{
		// Setup event processing
		this->EventCallbackCommand->SetCallback(AbstractWidget::ProcessEventsHandler);
		// There is no parent to this widget currently
		this->Parent = nullptr;
		// Set priority higher than interactor styles
		this->Priority = 0.5;
		// Does this widget respond to interaction?
		this->ProcessEvents = 1;
		// Okay, set up the event translations for the subclasses.
		this->EventTranslator = WidgetEventTranslator::New();
		this->CallbackMapper = WidgetCallbackMapper::New();
		this->CallbackMapper->SetEventTranslator(this->EventTranslator);
	}

	AbstractWidget::~AbstractWidget()
	{
		this->SetEnabled(0);
	}

	void AbstractWidget::SetEnabled(int enabling)
	{
		if (enabling) //----------------
		{
			if (this->Enabled) // already enabled, just return
			{
				return;
			}
			if (!this->Interactor)
			{
				return;
			}
			// We're ready to enable
			this->Enabled = 1;

			// listen for the events found in the EventTranslator
			if (!this->Parent)
			{
				this->EventTranslator->AddEventsToInteractor(
					this->Interactor, this->EventCallbackCommand, this->Priority);
			}
			else
			{
				this->EventTranslator->AddEventsToParent(
					this->Parent, this->EventCallbackCommand, this->Priority);
			}
			this->InvokeEvent(ExecuteCommand::EnableEvent, nullptr);
		}
		else // disabling------------------
		{
			if (!this->Enabled) // already disabled, just return
			{
				return;
			}
			this->Enabled = 0;
			// don't listen for events any more
			if (!this->Parent)
			{
				this->Interactor->RemoveObserver(this->EventCallbackCommand);
			}
			else
			{
				this->Parent->RemoveObserver(this->EventCallbackCommand);
			}
			this->InvokeEvent(ExecuteCommand::DisableEvent, nullptr);
		}
	}

	//------------------------------------------------------------------------------
	void AbstractWidget::ProcessEventsHandler(
		EventObject* object, unsigned long GizmoEvent, void* clientdata, void* calldata)
	{
		AbstractWidget* self = reinterpret_cast<AbstractWidget*>(clientdata);

		// if ProcessEvents is Off, we ignore all interaction events.
		if (!self->GetProcessEvents())
		{
			return;
		}

		// if the event has data then get the translation using the
		// event data
		unsigned long widgetEvent = WidgetEvent::NoEvent;
		if (calldata && ExecuteCommand::EventHasData(GizmoEvent))
		{
			widgetEvent =
				self->EventTranslator->GetTranslation(GizmoEvent, static_cast<GizmoEventData*>(calldata));
		}
		else
		{
			int modifier = GizmoEvent::GetModifier(self->Interactor);

			// If neither the ctrl nor the shift keys are pressed, give
			// NoModifier a preference over AnyModifier.
			if (modifier == GizmoEvent::AnyModifier)
			{
				widgetEvent = self->EventTranslator->GetTranslation(GizmoEvent, GizmoEvent::NoModifier,
					self->Interactor->GetKeyCode(), self->Interactor->GetRepeatCount(),
					self->Interactor->GetKeySym());
			}

			if (widgetEvent == WidgetEvent::NoEvent)
			{
				widgetEvent =self->EventTranslator->GetTranslation(GizmoEvent, modifier, self->Interactor->GetKeyCode(),
						self->Interactor->GetRepeatCount(), self->Interactor->GetKeySym());
			}
		}

		// Save the call data for widgets if needed
		self->CallData = calldata;

		// Invoke the widget callback
		if (widgetEvent != WidgetEvent::NoEvent)
		{
			self->CallbackMapper->InvokeCallback(widgetEvent);
		}
	}

	//------------------------------------------------------------------------------
	void AbstractWidget::Render()
	{
		if (!this->Parent && this->Interactor)
		{

		}
	}

	//------------------------------------------------------------------------------
	void AbstractWidget::SetPriority(float f)
	{
		if (f != this->Priority)
		{
			this->InteractorObserver::SetPriority(f);

			if (this->Enabled)
			{
				if (this->Interactor)
				{
					this->Interactor->RemoveObserver(this->CharObserverTag);
					this->Interactor->RemoveObserver(this->DeleteObserverTag);
					this->CharObserverTag = this->Interactor->AddObserver(
						ExecuteCommand::CharEvent, this->KeyPressCallbackCommand, this->Priority);
					this->DeleteObserverTag = this->Interactor->AddObserver(
						ExecuteCommand::DeleteEvent, this->KeyPressCallbackCommand, this->Priority);
				}

				if (!this->Parent)
				{
					if (this->Interactor)
					{
						this->Interactor->RemoveObserver(this->EventCallbackCommand);
					}
				}
				else
				{
					this->Parent->RemoveObserver(this->EventCallbackCommand);
				}

				if (!this->Parent)
				{
					if (this->Interactor)
					{
						this->EventTranslator->AddEventsToInteractor(
							this->Interactor, this->EventCallbackCommand, this->Priority);
					}
				}
				else
				{
					this->EventTranslator->AddEventsToParent(
						this->Parent, this->EventCallbackCommand, this->Priority);
				}
			}
		}
	}
}