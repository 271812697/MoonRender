#pragma once
#include <Rendering/Overlay/Interactive/EventObject.h>
namespace MOON {
	class WidgetEvent : public EventObject
	{
	public:
		static WidgetEvent* New();
		enum WidgetEventIds
		{
			NoEvent = 0,
			Select,
			EndSelect,
			Delete,
			Translate,
			EndTranslate,
			Scale,
			EndScale,
			Resize,
			EndResize,
			Rotate,
			EndRotate,
			Move,
			SizeHandles,
			AddPoint,
			AddFinalPoint,
			Completed,
			PickPoint,
			PickNormal,
			PickDirectionPoint,
			TimedOut,
			ModifyEvent,
			Reset,
			Up,
			Down,
			Left,
			Right,
			Select3D,
			EndSelect3D,
			Move3D,
			AddPoint3D,
			AddFinalPoint3D,
			HoverLeave
		};

		static const char* GetStringFromEventId(unsigned long event);
		static unsigned long GetEventIdFromString(const char* event);

	protected:
		WidgetEvent() = default;
		~WidgetEvent() override = default;
	private:
		WidgetEvent(const WidgetEvent&) = delete;
		void operator=(const WidgetEvent&) = delete;
	};

}