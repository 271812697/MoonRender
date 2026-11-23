#include <Rendering/Overlay/Interactive/Event.h>
#include <Rendering/Overlay/Interactive/ExecuteCommand.h>
#include <Rendering/Overlay/Interactive/RenderWindowInteractor.h>

namespace MOON {
	GizmoEvent::GizmoEvent()
	{
		this->Modifier = GizmoEvent::AnyModifier;
		this->KeyCode = 0;
		this->RepeatCount = 0;
		this->KeySym = nullptr;
		this->EventId = ExecuteCommand::NoEvent;
	}

	GizmoEvent::~GizmoEvent()
	{
		delete[] this->KeySym;
	}

	bool GizmoEvent::operator==(unsigned long GIZMOEvent) const
	{
		return this->EventId == GIZMOEvent;
	}

	bool GizmoEvent::operator==(GizmoEvent* e) const
	{
		if (this->EventId != e->EventId)
		{
			return false;
		}
		if (this->Modifier != GizmoEvent::AnyModifier && e->Modifier != GizmoEvent::AnyModifier &&
			this->Modifier != e->Modifier)
		{
			return false;
		}
		if (this->KeyCode != '\0' && e->KeyCode != '\0' && this->KeyCode != e->KeyCode)
		{
			return false;
		}
		if (this->RepeatCount != 0 && e->RepeatCount != 0 && this->RepeatCount != e->RepeatCount)
		{
			return false;
		}
		if (this->KeySym != nullptr && e->KeySym != nullptr && strcmp(this->KeySym, e->KeySym) != 0)
		{
			return false;
		}

		return true;
	}

	GizmoEvent* GizmoEvent::New()
	{
		auto result = new GizmoEvent;
		return result;
	}

	int GizmoEvent::GetModifier(RenderWindowInteractor* i)
	{
		int modifier = 0;
		modifier |= (i->GetShiftKey() ? GizmoEvent::ShiftModifier : 0);
		modifier |= (i->GetControlKey() ? GizmoEvent::ControlModifier : 0);
		modifier |= (i->GetAltKey() ? GizmoEvent::AltModifier : 0);

		return modifier;
	}


}
