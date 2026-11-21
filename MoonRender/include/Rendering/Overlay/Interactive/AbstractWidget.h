#pragma once
#include <Rendering/Overlay/Interactive/EventObserver.h>

namespace MOON {
	class WidgetEventTranslator;
	class WidgetCallbackMapper;
	class  AbstractWidget
		: public InteractorObserver
	{
	public:
		void SetEnabled(int) override;
		virtual void SetProcessEvents(bool _arg)
		{
			if (this->ProcessEvents != _arg)
			{
				this->ProcessEvents = _arg;
			}
		}
		virtual bool GetProcessEventsMinValue() { return 0; }
		virtual bool GetProcessEventsMaxValue() { return 1; };
		virtual bool GetProcessEvents()
		{
			return this->ProcessEvents;
		}
		virtual void ProcessEventsOn() { this->SetProcessEvents(static_cast<bool>(1)); }
		virtual void ProcessEventsOff() { this->SetProcessEvents(static_cast<bool>(0)); };
		WidgetEventTranslator* GetEventTranslator() { return this->EventTranslator; }
		virtual void CreateDefaultRepresentation() {};
		void Render();
		void SetParent(AbstractWidget* parent) { this->Parent = parent; }
		virtual AbstractWidget* GetParent()
		{
			return this->Parent;
		};
		void SetPriority(float) override;
	protected:
		AbstractWidget();
		~AbstractWidget() override;
		static void ProcessEventsHandler(EventObject* object, unsigned long event, void* clientdata, void* calldata);
		WidgetEventTranslator* EventTranslator;
		WidgetCallbackMapper* CallbackMapper;
		AbstractWidget* Parent;
		void* CallData;
		bool ProcessEvents;
	private:
		AbstractWidget(const AbstractWidget&) = delete;
		void operator=(const AbstractWidget&) = delete;
	};

}