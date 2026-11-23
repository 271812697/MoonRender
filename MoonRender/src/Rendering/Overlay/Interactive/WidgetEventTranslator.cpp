#include <Rendering/Overlay/Interactive/WidgetEventTranslator.h>
#include <Rendering/Overlay/Interactive/AbstractWidget.h>
#include <Rendering/Overlay/Interactive/CallbackCommand.h>
#include <Rendering/Overlay/Interactive/ExecuteCommand.h>
#include <Rendering/Overlay/Interactive/Event.h>
#include <Rendering/Overlay/Interactive/EventData.h>
#include <Rendering/Overlay/Interactive/RenderWindowInteractor.h>
#include <Rendering/Overlay/Interactive/WidgetEvent.h>
#include <list>
#include <map>
#include <memory>


namespace MOON {

	// This is what is place in the list
	struct EventItem
	{
		std::shared_ptr<GizmoEvent> GIZMOEvent;
		unsigned long WidgetEvent;
		GizmoEventData* EventData = nullptr;
		bool HasData = false;

		EventItem(const std::shared_ptr<GizmoEvent>& e, unsigned long we)
		{
			this->GIZMOEvent = e;
			this->WidgetEvent = we;
			this->HasData = false;
		}
		EventItem(GizmoEventData* edata, unsigned long we)
		{
			this->EventData = edata;
			//this->EventData->Register(nullptr);
			this->WidgetEvent = we;
			this->HasData = true;
		}
		~EventItem()
		{
			if (this->HasData && this->EventData)
			{
				//this->EventData->UnRegister(nullptr);
				this->EventData = nullptr;
			}
		}

		EventItem(const EventItem& v)
		{
			this->GIZMOEvent = v.GIZMOEvent;
			this->WidgetEvent = v.WidgetEvent;
			this->HasData = v.HasData;
			this->EventData = v.EventData;
			if (this->HasData && this->EventData)
			{
				// this->EventData->Register(nullptr);
			}
		}

	private:
		EventItem() = delete;
	};

	// A list of events
	struct EventList : public std::list<EventItem>
	{
		unsigned long find(unsigned long GIZMOEvent)
		{
			std::list<EventItem>::iterator liter = this->begin();
			for (; liter != this->end(); ++liter)
			{
				if (GIZMOEvent == liter->GIZMOEvent->GetEventId())
				{
					return liter->WidgetEvent;
				}
			}
			return WidgetEvent::NoEvent;
		}

		unsigned long find(GizmoEvent* GIZMOEvent)
		{
			std::list<EventItem>::iterator liter = this->begin();
			for (; liter != this->end(); ++liter)
			{
				if (*GIZMOEvent == liter->GIZMOEvent.get())
				{
					return liter->WidgetEvent;
				}
			}
			return WidgetEvent::NoEvent;
		}

		unsigned long find(GizmoEventData* edata)
		{
			std::list<EventItem>::iterator liter = this->begin();
			for (; liter != this->end(); ++liter)
			{
				if (liter->HasData && *edata == *liter->EventData)
				{
					return liter->WidgetEvent;
				}
			}
			return WidgetEvent::NoEvent;
		}

		// Remove a mapping
		int Remove(GizmoEvent* GIZMOEvent)
		{
			std::list<EventItem>::iterator liter = this->begin();
			for (; liter != this->end(); ++liter)
			{
				if (*GIZMOEvent == liter->GIZMOEvent.get())
				{
					this->erase(liter);
					return 1;
				}
			}
			return 0;
		}
		int Remove(GizmoEventData* edata)
		{
			std::list<EventItem>::iterator liter = this->begin();
			for (; liter != this->end(); ++liter)
			{
				if (liter->HasData && *edata == *liter->EventData)
				{
					this->erase(liter);
					return 1;
				}
			}
			return 0;
		}
	};

	// A STL map used to translate  events into lists of events. The reason
	// that we have this list is because of the modifiers on the event. The
	//  event id maps to the list, and then comparisons are done to
	// determine which event matches.
	class EventMap : public std::map<unsigned long, EventList>
	{
	};
	using EventMapIterator = std::map<unsigned long, EventList>::iterator;

	//------------------------------------------------------------------------------
	WidgetEventTranslator* WidgetEventTranslator::New() {
		return new WidgetEventTranslator();
	}
	WidgetEventTranslator::WidgetEventTranslator()
	{
		this->eventMap = new EventMap;
		this->Event = GizmoEvent::New();
	}

	//------------------------------------------------------------------------------
	WidgetEventTranslator::~WidgetEventTranslator()
	{
		delete this->eventMap;
		//this->Event->Delete();
	}

	//------------------------------------------------------------------------------
	void WidgetEventTranslator::SetTranslation(unsigned long GIZMOEvent, unsigned long widgetEvent)
	{
		std::shared_ptr<GizmoEvent> e = std::make_shared<GizmoEvent>();
		e->SetEventId(GIZMOEvent); // default modifiers
		if (widgetEvent != WidgetEvent::NoEvent)
		{
			(*this->eventMap)[GIZMOEvent].push_back(EventItem(e, widgetEvent));
		}
		else
		{
			this->RemoveTranslation(e.get());
		}
	}

	//------------------------------------------------------------------------------
	void WidgetEventTranslator::SetTranslation(const char* GIZMOEvent, const char* widgetEvent)
	{
		this->SetTranslation(
			ExecuteCommand::GetEventIdFromString(GIZMOEvent), WidgetEvent::GetEventIdFromString(widgetEvent));
	}

	//------------------------------------------------------------------------------
	void WidgetEventTranslator::SetTranslation(unsigned long GIZMOEvent, int modifier, char keyCode,
		int repeatCount, const char* keySym, unsigned long widgetEvent)
	{
		std::shared_ptr<GizmoEvent> e = std::make_shared<GizmoEvent>();
		e->SetEventId(GIZMOEvent);
		e->SetModifier(modifier);
		e->SetKeyCode(keyCode);
		e->SetRepeatCount(repeatCount);
		e->SetKeySym(keySym);
		if (widgetEvent != WidgetEvent::NoEvent)
		{
			(*this->eventMap)[GIZMOEvent].push_back(EventItem(e, widgetEvent));
		}
		else
		{
			this->RemoveTranslation(e.get());
		}
	}

	void WidgetEventTranslator::SetTranslation(
		unsigned long GIZMOEvent, GizmoEventData* edata, unsigned long widgetEvent)
	{
		if (widgetEvent != WidgetEvent::NoEvent)
		{
			(*this->eventMap)[GIZMOEvent].push_back(EventItem(edata, widgetEvent));
		}
		else
		{
			this->RemoveTranslation(edata);
		}
	}

	//------------------------------------------------------------------------------
	void WidgetEventTranslator::SetTranslation(GizmoEvent* GIZMOEvent, unsigned long widgetEvent)
	{
		if (widgetEvent != WidgetEvent::NoEvent)
		{
			(*this->eventMap)[GIZMOEvent->GetEventId()].push_back(EventItem(std::shared_ptr<GizmoEvent>(GIZMOEvent), widgetEvent));
		}
		else
		{
			this->RemoveTranslation(GIZMOEvent);
		}
	}

	//------------------------------------------------------------------------------
	unsigned long WidgetEventTranslator::GetTranslation(unsigned long GIZMOEvent)
	{
		EventMapIterator iter = this->eventMap->find(GIZMOEvent);
		if (iter != this->eventMap->end())
		{
			EventList& elist = (*iter).second;
			return elist.find(GIZMOEvent);
		}
		else
		{
			return WidgetEvent::NoEvent;
		}
	}

	//------------------------------------------------------------------------------
	const char* WidgetEventTranslator::GetTranslation(const char* GIZMOEvent)
	{
		return WidgetEvent::GetStringFromEventId(
			this->GetTranslation(ExecuteCommand::GetEventIdFromString(GIZMOEvent)));
	}

	//------------------------------------------------------------------------------
	unsigned long WidgetEventTranslator::GetTranslation(
		unsigned long GIZMOEvent, int modifier, char keyCode, int repeatCount, const char* keySym)
	{
		EventMapIterator iter = this->eventMap->find(GIZMOEvent);
		if (iter != this->eventMap->end())
		{
			this->Event->SetEventId(GIZMOEvent);
			this->Event->SetModifier(modifier);
			this->Event->SetKeyCode(keyCode);
			this->Event->SetRepeatCount(repeatCount);
			this->Event->SetKeySym(keySym);
			EventList& elist = (*iter).second;
			return elist.find(this->Event);
		}
		return WidgetEvent::NoEvent;
	}

	//------------------------------------------------------------------------------
	unsigned long WidgetEventTranslator::GetTranslation(unsigned long, GizmoEventData* edata)
	{
		EventMapIterator iter = this->eventMap->find(edata->GetType());
		if (iter != this->eventMap->end())
		{
			EventList& elist = (*iter).second;
			return elist.find(edata);
		}
		return WidgetEvent::NoEvent;
	}

	//------------------------------------------------------------------------------
	unsigned long WidgetEventTranslator::GetTranslation(GizmoEvent* GIZMOEvent)
	{
		EventMapIterator iter = this->eventMap->find(GIZMOEvent->GetEventId());
		if (iter != this->eventMap->end())
		{
			EventList& elist = (*iter).second;
			return elist.find(GIZMOEvent);
		}
		else
		{
			return WidgetEvent::NoEvent;
		}
	}

	//------------------------------------------------------------------------------
	int WidgetEventTranslator::RemoveTranslation(
		unsigned long GIZMOEvent, int modifier, char keyCode, int repeatCount, const char* keySym)
	{
		std::shared_ptr<GizmoEvent> e = std::make_shared<GizmoEvent>();
		e->SetEventId(GIZMOEvent);
		e->SetModifier(modifier);
		e->SetKeyCode(keyCode);
		e->SetRepeatCount(repeatCount);
		e->SetKeySym(keySym);
		return this->RemoveTranslation(e.get());
	}

	//------------------------------------------------------------------------------
	int WidgetEventTranslator::RemoveTranslation(GizmoEvent* e)
	{
		EventMapIterator iter = this->eventMap->find(e->GetEventId());
		int numTranslationsRemoved = 0;
		if (iter != this->eventMap->end())
		{
			while (iter->second.Remove(e))
			{
				++numTranslationsRemoved;
				iter = this->eventMap->find(e->GetEventId());
				if (iter == this->eventMap->end())
				{
					break;
				}
			}
		}

		return numTranslationsRemoved;
	}

	//------------------------------------------------------------------------------
	int WidgetEventTranslator::RemoveTranslation(GizmoEventData* edata)
	{
		EventMapIterator iter = this->eventMap->find(edata->GetType());
		int numTranslationsRemoved = 0;
		if (iter != this->eventMap->end())
		{
			while (iter->second.Remove(edata))
			{
				++numTranslationsRemoved;
				iter = this->eventMap->find(edata->GetType());
				if (iter == this->eventMap->end())
				{
					break;
				}
			}
		}

		return numTranslationsRemoved;
	}

	//------------------------------------------------------------------------------
	int WidgetEventTranslator::RemoveTranslation(unsigned long GIZMOEvent)
	{
		std::shared_ptr<GizmoEvent> e = std::make_shared<GizmoEvent>();
		e->SetEventId(GIZMOEvent);
		return this->RemoveTranslation(e.get());
	}

	//------------------------------------------------------------------------------
	int WidgetEventTranslator::RemoveTranslation(const char* GIZMOEvent)
	{
		std::shared_ptr<GizmoEvent> e = std::make_shared<GizmoEvent>();
		e->SetEventId(ExecuteCommand::GetEventIdFromString(GIZMOEvent));
		return this->RemoveTranslation(e.get());
	}

	//------------------------------------------------------------------------------
	void WidgetEventTranslator::ClearEvents()
	{
		EventMapIterator iter = this->eventMap->begin();
		for (; iter != this->eventMap->end(); ++iter)
		{
			EventList& elist = (*iter).second;
			elist.clear();
		}
		this->eventMap->clear();
	}

	//------------------------------------------------------------------------------
	void WidgetEventTranslator::AddEventsToInteractor(
		RenderWindowInteractor* i, CallbackCommand* command, float priority)
	{
		EventMapIterator iter = this->eventMap->begin();
		for (; iter != this->eventMap->end(); ++iter)
		{
			i->AddObserver((*iter).first, command, priority);
		}
	}

	//------------------------------------------------------------------------------
	void WidgetEventTranslator::AddEventsToParent(
		AbstractWidget* w, CallbackCommand* command, float priority)
	{
		EventMapIterator iter = this->eventMap->begin();
		for (; iter != this->eventMap->end(); ++iter)
		{
			w->AddObserver((*iter).first, command, priority);
		}
	}
}