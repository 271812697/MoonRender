#include <Rendering/Overlay/Interactive/WidgetCallbackMapper.h>
#include <Rendering/Overlay/Interactive/AbstractWidget.h>
#include <Rendering/Overlay/Interactive/ExecuteCommand.h>
#include <Rendering/Overlay/Interactive/EventData.h>
#include <Rendering/Overlay/Interactive/WidgetEventTranslator.h>
#include <map>
namespace MOON {
	// Callbacks are stored as a pair of (Object,Method) in the map.
	struct CallbackPair
	{
		CallbackPair()
			: Widget(nullptr)
			, Callback(nullptr)
		{
		} // map requires empty constructor
		CallbackPair(AbstractWidget* w, WidgetCallbackMapper::CallbackType f)
			: Widget(w)
			, Callback(f)
		{
		}

		AbstractWidget* Widget;
		WidgetCallbackMapper::CallbackType Callback;
	};

	// The map tracks the correspondence between widget events and callbacks
	class CallbackMap : public std::map<unsigned long, CallbackPair>
	{
	public:
		typedef CallbackMap CallbackMapType;
		typedef std::map<unsigned long, CallbackPair>::iterator CallbackMapIterator;
	};

	//------------------------------------------------------------------------------
	WidgetCallbackMapper::WidgetCallbackMapper()
	{
		this->callbackMap = new CallbackMap;
		this->EventTranslator = nullptr;
	}

	//------------------------------------------------------------------------------
	WidgetCallbackMapper::~WidgetCallbackMapper()
	{
		delete this->callbackMap;
		if (this->EventTranslator)
		{
			//this->EventTranslator->Delete();
		}
	}

	WidgetCallbackMapper* MOON::WidgetCallbackMapper::New()
	{
		return new WidgetCallbackMapper();
	}

	//------------------------------------------------------------------------------
	void WidgetCallbackMapper::SetEventTranslator(WidgetEventTranslator* t)
	{
		if (this->EventTranslator != t)
		{
			if (this->EventTranslator)
			{
				//this->EventTranslator->Delete();
			}
			this->EventTranslator = t;
			if (this->EventTranslator)
			{
				//this->EventTranslator->Register(this);
			}
		}
	}
	//------------------------------------------------------------------------------
	void WidgetCallbackMapper::SetCallbackMethod(
		unsigned long GIZMOEvent, unsigned long widgetEvent, AbstractWidget* w, CallbackType f)
	{
		this->EventTranslator->SetTranslation(GIZMOEvent, widgetEvent);
		this->SetCallbackMethod(widgetEvent, w, f);
	}

	//------------------------------------------------------------------------------
	void WidgetCallbackMapper::SetCallbackMethod(unsigned long GIZMOEvent, int modifier, char keyCode,
		int repeatCount, const char* keySym, unsigned long widgetEvent, AbstractWidget* w,
		CallbackType f)
	{
		this->EventTranslator->SetTranslation(
			GIZMOEvent, modifier, keyCode, repeatCount, keySym, widgetEvent);
		this->SetCallbackMethod(widgetEvent, w, f);
	}

	//------------------------------------------------------------------------------
	void WidgetCallbackMapper::SetCallbackMethod(unsigned long GIZMOEvent, GizmoEventData* edata,
		unsigned long widgetEvent, AbstractWidget* w, CallbackType f)
	{
		// make sure the type is set
		edata->SetType(GIZMOEvent);
		this->EventTranslator->SetTranslation(GIZMOEvent, edata, widgetEvent);
		this->SetCallbackMethod(widgetEvent, w, f);
	}

	//------------------------------------------------------------------------------
	void WidgetCallbackMapper::SetCallbackMethod(
		unsigned long widgetEvent, AbstractWidget* w, CallbackType f)
	{
		(*this->callbackMap)[widgetEvent] = CallbackPair(w, f);
	}
	//------------------------------------------------------------------------------
	void WidgetCallbackMapper::InvokeCallback(unsigned long widgetEvent)
	{
		CallbackMap::CallbackMapIterator iter = this->callbackMap->find(widgetEvent);
		if (iter != this->callbackMap->end())
		{
			AbstractWidget* w = (*iter).second.Widget;
			CallbackType f = (*iter).second.Callback;
			(*f)(w);
		}
	}
}