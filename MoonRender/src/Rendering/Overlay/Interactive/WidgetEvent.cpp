#include <Rendering/Overlay/Interactive/WidgetEvent.h>
#include <algorithm>
namespace MOON {
	static const char* WidgetEventStrings[] = {
		  "NoEvent",
		  "Select",
		  "EndSelect",
		  "Delete",
		  "Translate",
		  "EndTranslate",
		  "Scale",
		  "EndScale",
		  "Resize",
		  "EndResize",
		  "Rotate",
		  "EndRotate",
		  "Move",
		  "SizeHandles",
		  "AddPoint",
		  "AddFinalPoint",
		  "Completed",
		  "TimedOut",
		  "ModifyEvent",
		  "Reset",
		  "HoverLeave",
		  nullptr,
	};
	WidgetEvent* WidgetEvent::New()
	{
		auto result = new WidgetEvent;
		return result;
	}

	//------------------------------------------------------------------------------
	const char* WidgetEvent::GetStringFromEventId(unsigned long event)
	{
		static unsigned long numevents = 0;
		// find length of table
		if (!numevents)
		{
			while (WidgetEventStrings[numevents] != nullptr)
			{
				numevents++;
			}
		}
		if (event < numevents)
		{
			return WidgetEventStrings[event];
		}
		else
		{
			return "NoEvent";
		}
	}
	//------------------------------------------------------------------------------
	unsigned long WidgetEvent::GetEventIdFromString(const char* event)
	{
		unsigned long i;
		for (i = 0; WidgetEventStrings[i] != nullptr; i++)
		{
			if (!strcmp(WidgetEventStrings[i], event))
			{
				return i;
			}
		}
		return WidgetEvent::NoEvent;
	}
}