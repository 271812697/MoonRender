#include<algorithm>
#include <Rendering/Overlay/Interactive/ExecuteCommand.h>

namespace MOON {
	ExecuteCommand::ExecuteCommand()
		: AbortFlag(0)
		, PassiveObserver(0)
	{

	}

	//----------------------------------------------------------------
	const char* ExecuteCommand::GetStringFromEventId(unsigned long event)
	{
		switch (event)
		{
		case AnyEvent: return "AnyEvent";
		case DeleteEvent: return "DeleteEvent";
		case StartEvent: return "StartEvent";
		case EndEvent: return "EndEvent";
		case RenderEvent: return "RenderEvent";
		case PickEvent: return "PickEvent";
		case StartPickEvent: return "StartPickEvent";
		case EndPickEvent: return "EndPickEvent";
		case LeftButtonPressEvent: return "LeftButtonPressEvent";
		case LeftButtonReleaseEvent: return "LeftButtonReleaseEvent";
		case MiddleButtonPressEvent: return "MiddleButtonPressEvent";
		case MiddleButtonReleaseEvent: return "MiddleButtonReleaseEvent";
		case RightButtonPressEvent: return "RightButtonPressEvent";
		case RightButtonReleaseEvent: return "RightButtonReleaseEvent";
		case EnterEvent: return "EnterEvent";
		case LeaveEvent: return "LeaveEvent";
		case KeyPressEvent: return "KeyPressEvent";
		case KeyReleaseEvent: return "KeyReleaseEvent";
		case CharEvent: return "CharEvent";
		case MouseMoveEvent: return "MouseMoveEvent";
		case MouseWheelForwardEvent: return "MouseWheelForwardEvent";
		case MouseWheelBackwardEvent: return "MouseWheelBackwardEvent";
		case MouseWheelLeftEvent: return "MouseWheelLeftEvent";
		case MouseWheelRightEvent:return "MouseWheelRightEvent";
		case StartInteractionEvent: return "StartInteractionEvent";
		case InteractionEvent: return "InteractionEvent";
		case EndInteractionEvent: return "EndInteractionEvent";
		case EnableEvent: return "EnableEvent";
		case DisableEvent: return "DisableEvent";
		case UserEvent:return "UserEvent";
		case NoEvent: return "NoEvent";

		case ExitEvent:return "ExitEvent";

		}
		return "NoEvent";
	}

	//----------------------------------------------------------------
	unsigned long ExecuteCommand::GetEventIdFromString(const char* event)
	{
		if (event)
		{
			if (strcmp(event, "AnyEvent") == 0) {
				return AnyEvent;
			} if (strcmp(event, "DeleteEvent") == 0) {
				return DeleteEvent;
			} if (strcmp(event, "StartEvent") == 0) {
				return StartEvent;
			} if (strcmp(event, "EndEvent") == 0) {
				return EndEvent;
			} if (strcmp(event, "RenderEvent") == 0) {
				return RenderEvent;
			} if (strcmp(event, "PickEvent") == 0) {
				return PickEvent;
			} if (strcmp(event, "StartPickEvent") == 0) {
				return StartPickEvent;
			} if (strcmp(event, "EndPickEvent") == 0) {
				return EndPickEvent;
			} if (strcmp(event, "LeftButtonPressEvent") == 0) {
				return LeftButtonPressEvent;
			} if (strcmp(event, "LeftButtonReleaseEvent") == 0) {
				return LeftButtonReleaseEvent;
			} if (strcmp(event, "MiddleButtonPressEvent") == 0) {
				return MiddleButtonPressEvent;
			} if (strcmp(event, "MiddleButtonReleaseEvent") == 0) {
				return MiddleButtonReleaseEvent;
			} if (strcmp(event, "RightButtonPressEvent") == 0) {
				return RightButtonPressEvent;
			} if (strcmp(event, "RightButtonReleaseEvent") == 0) {
				return RightButtonReleaseEvent;
			} if (strcmp(event, "EnterEvent") == 0) {
				return EnterEvent;
			} if (strcmp(event, "LeaveEvent") == 0) {
				return LeaveEvent;
			} if (strcmp(event, "KeyPressEvent") == 0) {
				return KeyPressEvent;
			} if (strcmp(event, "KeyReleaseEvent") == 0) {
				return KeyReleaseEvent;
			} if (strcmp(event, "CharEvent") == 0) {
				return CharEvent;
			} if (strcmp(event, "MouseMoveEvent") == 0) {
				return MouseMoveEvent;
			} if (strcmp(event, "MouseWheelForwardEvent") == 0) {
				return MouseWheelForwardEvent;
			} if (strcmp(event, "MouseWheelBackwardEvent") == 0) {
				return MouseWheelBackwardEvent;
			} if (strcmp(event, "MouseWheelRightEvent") == 0) {
				return ExecuteCommand::MouseWheelRightEvent;
			}if (strcmp(event, "MouseWheelLeftEvent") == 0) {
				return ExecuteCommand::MouseWheelLeftEvent;
			}
			if (strcmp(event, "StartInteractionEvent") == 0) {
				return StartInteractionEvent;
			} if (strcmp(event, "InteractionEvent") == 0) {
				return InteractionEvent;
			} if (strcmp(event, "EndInteractionEvent") == 0) {
				return EndInteractionEvent;
			} if (strcmp(event, "EnableEvent") == 0) {
				return EnableEvent;
			} if (strcmp(event, "DisableEvent") == 0) {
				return DisableEvent;
			}
			if (strcmp("UserEvent", event) == 0)
			{
				return ExecuteCommand::UserEvent;
			}
			if (strcmp("ExitEvent", event) == 0) {
				return ExecuteCommand::ExitEvent;
			}
		}
		return ExecuteCommand::NoEvent;
	}

	bool ExecuteCommand::EventHasData(unsigned long event)
	{
		return false;
	}


}
