#pragma once
#include <Rendering/Overlay/Interactive/EventObject.h>
namespace MOON {
	class WidgetEvent;
	class AbstractWidget;
	class WidgetEventTranslator;
	class CallbackMap; // PIMPL encapsulation of STL map
	class GizmoEventData;
	class WidgetCallbackMapper : public EventObject
	{
	public:

		static WidgetCallbackMapper* New();

		void SetEventTranslator(WidgetEventTranslator* t);
		virtual WidgetEventTranslator* GetEventTranslator()
		{
			return this->EventTranslator;
		};
		///@}

		typedef void (*CallbackType)(AbstractWidget*);


		void SetCallbackMethod(
			unsigned long GIZMOEvent, unsigned long widgetEvent, AbstractWidget* w, CallbackType f);
		void SetCallbackMethod(unsigned long GIZMOEvent, int modifiers, char keyCode, int repeatCount,
			const char* keySym, unsigned long widgetEvent, AbstractWidget* w, CallbackType f);
		void SetCallbackMethod(unsigned long GIZMOEvent, GizmoEventData* ed, unsigned long widgetEvent,
			AbstractWidget* w, CallbackType f);

		void InvokeCallback(unsigned long widgetEvent);

	protected:
		WidgetCallbackMapper();
		~WidgetCallbackMapper() override;
		WidgetEventTranslator* EventTranslator;
		CallbackMap* callbackMap;
		void SetCallbackMethod(unsigned long widgetEvent, AbstractWidget* w, CallbackType f);

	private:
		WidgetCallbackMapper(const WidgetCallbackMapper&) = delete;
		void operator=(const WidgetCallbackMapper&) = delete;
	};

}