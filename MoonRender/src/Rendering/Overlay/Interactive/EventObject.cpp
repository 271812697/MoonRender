#include <Rendering/Overlay/Interactive/EventObject.h>
#include <Rendering/Overlay/Interactive/ExecuteCommand.h>

#include <algorithm>
#include <sstream>
#include <vector>

namespace MOON {
	class EventObserver
	{
	public:
		EventObserver()
			: Command(nullptr)
			, Event(0)
			, Tag(0)
			, Next(nullptr)
			, Priority(0.0)
		{
		}
		~EventObserver();


		ExecuteCommand* Command;
		unsigned long Event;
		unsigned long Tag;
		EventObserver* Next;
		float Priority;
	};

	class SubjectHelper
	{
	public:
		SubjectHelper()
			: Focus1(nullptr)
			, Focus2(nullptr)
			, Start(nullptr)
			, Count(1)
		{
		}
		~SubjectHelper();

		unsigned long AddObserver(unsigned long event, ExecuteCommand* cmd, float p);
		void RemoveObserver(unsigned long tag);
		void RemoveObservers(unsigned long event);
		void RemoveObservers(unsigned long event, ExecuteCommand* cmd);
		void RemoveAllObservers();
		bool InvokeEvent(unsigned long event, void* callData, EventObject* self);
		ExecuteCommand* GetCommand(unsigned long tag);
		unsigned long GetTag(ExecuteCommand*);
		bool HasObserver(unsigned long event);
		bool HasObserver(unsigned long event, ExecuteCommand* cmd);
		void GrabFocus(ExecuteCommand* c1, ExecuteCommand* c2)
		{
			this->Focus1 = c1;
			this->Focus2 = c2;
		}
		void ReleaseFocus()
		{
			this->Focus1 = nullptr;
			this->Focus2 = nullptr;
		}

		std::vector<bool> ListModified;

		ExecuteCommand* Focus1;
		ExecuteCommand* Focus2;

	protected:
		EventObserver* Start;
		unsigned long Count;
	};

	EventObject* EventObject::New()
	{
		EventObject* ret = new EventObject;

		return ret;
	}


	EventObject::EventObject()
	{

		this->subjectHelper = nullptr;

	}

	EventObject::~EventObject()
	{

		delete this->subjectHelper;
		this->subjectHelper = nullptr;
	}

	EventObserver::~EventObserver()
	{

	}

	SubjectHelper::~SubjectHelper()
	{
		EventObserver* elem = this->Start;
		EventObserver* next;
		while (elem)
		{
			next = elem->Next;
			delete elem;
			elem = next;
		}
		this->Start = nullptr;
		this->Focus1 = nullptr;
		this->Focus2 = nullptr;
	}

	unsigned long SubjectHelper::AddObserver(unsigned long event, ExecuteCommand* cmd, float p)
	{
		EventObserver* elem;

		// initialize the new observer element
		elem = new EventObserver;
		elem->Priority = p;
		elem->Next = nullptr;
		elem->Event = event;
		elem->Command = cmd;

		elem->Tag = this->Count;
		this->Count++;

		// now insert into the list
		// if no other elements in the list then this is Start
		if (!this->Start)
		{
			this->Start = elem;
		}
		else
		{
			// insert high priority first
			EventObserver* prev = nullptr;
			EventObserver* pos = this->Start;
			while (pos->Priority >= elem->Priority && pos->Next)
			{
				prev = pos;
				pos = pos->Next;
			}
			// pos is Start and elem should not be start
			if (pos->Priority > elem->Priority)
			{
				pos->Next = elem;
			}
			else
			{
				if (prev)
				{
					prev->Next = elem;
				}
				elem->Next = pos;
				// check to see if the new element is the start
				if (pos == this->Start)
				{
					this->Start = elem;
				}
			}
		}
		return elem->Tag;
	}

	void SubjectHelper::RemoveObserver(unsigned long tag)
	{
		EventObserver* elem;
		EventObserver* prev;
		EventObserver* next;

		elem = this->Start;
		prev = nullptr;
		while (elem)
		{
			if (elem->Tag == tag)
			{
				if (prev)
				{
					prev->Next = elem->Next;
					next = prev->Next;
				}
				else
				{
					this->Start = elem->Next;
					next = this->Start;
				}
				delete elem;
				elem = next;
			}
			else
			{
				prev = elem;
				elem = elem->Next;
			}
		}

		if (!this->ListModified.empty())
		{
			this->ListModified.assign(this->ListModified.size(), true);
		}
	}

	void SubjectHelper::RemoveObservers(unsigned long event)
	{
		EventObserver* elem;
		EventObserver* prev;
		EventObserver* next;

		elem = this->Start;
		prev = nullptr;
		while (elem)
		{
			if (elem->Event == event)
			{
				if (prev)
				{
					prev->Next = elem->Next;
					next = prev->Next;
				}
				else
				{
					this->Start = elem->Next;
					next = this->Start;
				}
				delete elem;
				elem = next;
			}
			else
			{
				prev = elem;
				elem = elem->Next;
			}
		}

		if (!this->ListModified.empty())
		{
			this->ListModified.assign(this->ListModified.size(), true);
		}
	}

	void SubjectHelper::RemoveObservers(unsigned long event, ExecuteCommand* cmd)
	{
		EventObserver* elem;
		EventObserver* prev;
		EventObserver* next;

		elem = this->Start;
		prev = nullptr;
		while (elem)
		{
			if (elem->Event == event && elem->Command == cmd)
			{
				if (prev)
				{
					prev->Next = elem->Next;
					next = prev->Next;
				}
				else
				{
					this->Start = elem->Next;
					next = this->Start;
				}
				delete elem;
				elem = next;
			}
			else
			{
				prev = elem;
				elem = elem->Next;
			}
		}

		if (!this->ListModified.empty())
		{
			this->ListModified.assign(this->ListModified.size(), true);
		}
	}

	void SubjectHelper::RemoveAllObservers()
	{
		EventObserver* elem = this->Start;
		EventObserver* next;
		while (elem)
		{
			next = elem->Next;
			delete elem;
			elem = next;
		}
		this->Start = nullptr;

		if (!this->ListModified.empty())
		{
			this->ListModified.assign(this->ListModified.size(), true);
		}
	}

	bool SubjectHelper::HasObserver(unsigned long event)
	{
		EventObserver* elem = this->Start;
		while (elem)
		{
			if (elem->Event == event || elem->Event == ExecuteCommand::AnyEvent)
			{
				return true;
			}
			elem = elem->Next;
		}
		return false;
	}

	bool SubjectHelper::HasObserver(unsigned long event, ExecuteCommand* cmd)
	{
		EventObserver* elem = this->Start;
		while (elem)
		{
			if ((elem->Event == event || elem->Event == ExecuteCommand::AnyEvent) && elem->Command == cmd)
			{
				return true;
			}
			elem = elem->Next;
		}
		return false;
	}

	bool SubjectHelper::InvokeEvent(unsigned long event, void* callData, EventObject* self)
	{
		bool focusHandled = false;


		this->ListModified.push_back(false);

		using  VisitedListType = std::vector<unsigned long>;
		VisitedListType visited;
		EventObserver* elem = this->Start;

		const unsigned long maxTag = this->Count;


		EventObserver* next;
		while (elem)
		{
			// store the next pointer because elem could disappear due to Command
			next = elem->Next;
			if (elem->Command->GetPassiveObserver() &&
				(elem->Event == event || elem->Event == ExecuteCommand::AnyEvent) && elem->Tag < maxTag)
			{
				VisitedListType::iterator vIter = std::lower_bound(visited.begin(), visited.end(), elem->Tag);
				if (vIter == visited.end() || *vIter != elem->Tag)
				{
					// Sorted insertion by tag to speed-up future searches at limited
					// insertion cost because it reuses the search iterator already at the
					// correct location
					visited.insert(vIter, elem->Tag);
					ExecuteCommand* command = elem->Command;

					elem->Command->Execute(self, event, callData);

				}
			}
			if (this->ListModified.back())
			{
				// Passive observer should not call AddObserver or RemoveObserver in callback.;
				elem = this->Start;
				this->ListModified.back() = false;
			}
			else
			{
				elem = next;
			}
		}

		// 1. Focus loop
		//
		if (this->Focus1 || this->Focus2)
		{
			elem = this->Start;
			while (elem)
			{
				// store the next pointer because elem could disappear due to Command
				next = elem->Next;
				if (((this->Focus1 == elem->Command) || (this->Focus2 == elem->Command)) &&
					(elem->Event == event || elem->Event == ExecuteCommand::AnyEvent) && elem->Tag < maxTag)
				{
					VisitedListType::iterator vIter =
						std::lower_bound(visited.begin(), visited.end(), elem->Tag);
					if (vIter == visited.end() || *vIter != elem->Tag)
					{
						// Don't execute the remainder loop
						focusHandled = true;
						// Sorted insertion by tag to speed-up future searches at limited
						// insertion cost because it reuses the search iterator already at the
						// correct location
						visited.insert(vIter, elem->Tag);
						ExecuteCommand* command = elem->Command;

						command->SetAbortFlag(false);
						elem->Command->Execute(self, event, callData);
						// if the command set the abort flag, then stop firing events
						// and return
						if (command->GetAbortFlag())
						{

							this->ListModified.pop_back();
							return true;
						}

					}
				}
				if (this->ListModified.back())
				{
					elem = this->Start;
					this->ListModified.back() = false;
				}
				else
				{
					elem = next;
				}
			}
		}

		// 2. Remainder loop
		//
		if (!focusHandled)
		{
			elem = this->Start;
			while (elem)
			{
				// store the next pointer because elem could disappear due to Command
				next = elem->Next;
				if ((elem->Event == event || elem->Event == ExecuteCommand::AnyEvent) && elem->Tag < maxTag)
				{
					VisitedListType::iterator vIter =
						std::lower_bound(visited.begin(), visited.end(), elem->Tag);
					if (vIter == visited.end() || *vIter != elem->Tag)
					{
						// Sorted insertion by tag to speed-up future searches at limited
						// insertion cost because it reuses the search iterator already at the
						// correct location
						visited.insert(vIter, elem->Tag);
						ExecuteCommand* command = elem->Command;

						command->SetAbortFlag(false);
						elem->Command->Execute(self, event, callData);
						// if the command set the abort flag, then stop firing events
						// and return
						if (command->GetAbortFlag())
						{

							this->ListModified.pop_back();
							return true;
						}

					}
				}
				if (this->ListModified.back())
				{
					elem = this->Start;
					this->ListModified.back() = false;
				}
				else
				{
					elem = next;
				}
			}
		}

		this->ListModified.pop_back();
		return false;
	}

	unsigned long SubjectHelper::GetTag(ExecuteCommand* cmd)
	{
		EventObserver* elem = this->Start;
		while (elem)
		{
			if (elem->Command == cmd)
			{
				return elem->Tag;
			}
			elem = elem->Next;
		}
		return 0;
	}

	ExecuteCommand* SubjectHelper::GetCommand(unsigned long tag)
	{
		EventObserver* elem = this->Start;
		while (elem)
		{
			if (elem->Tag == tag)
			{
				return elem->Command;
			}
			elem = elem->Next;
		}
		return nullptr;
	}

	unsigned long EventObject::AddObserver(unsigned long event, ExecuteCommand* cmd, float p)
	{
		if (!this->subjectHelper)
		{
			this->subjectHelper = new SubjectHelper;
		}
		return this->subjectHelper->AddObserver(event, cmd, p);
	}

	unsigned long EventObject::AddObserver(const char* event, ExecuteCommand* cmd, float p)
	{
		return this->AddObserver(ExecuteCommand::GetEventIdFromString(event), cmd, p);
	}

	ExecuteCommand* EventObject::GetExecuteCommand(unsigned long tag)
	{
		if (this->subjectHelper)
		{
			return this->subjectHelper->GetCommand(tag);
		}
		return nullptr;
	}

	void EventObject::RemoveObserver(unsigned long tag)
	{
		if (this->subjectHelper)
		{
			this->subjectHelper->RemoveObserver(tag);
		}
	}

	void EventObject::RemoveObserver(ExecuteCommand* c)
	{
		if (this->subjectHelper)
		{
			unsigned long tag = this->subjectHelper->GetTag(c);
			while (tag)
			{
				this->subjectHelper->RemoveObserver(tag);
				tag = this->subjectHelper->GetTag(c);
			}
		}
	}

	void EventObject::RemoveObservers(unsigned long event)
	{
		if (this->subjectHelper)
		{
			this->subjectHelper->RemoveObservers(event);
		}
	}

	void EventObject::RemoveObservers(const char* event)
	{
		this->RemoveObservers(ExecuteCommand::GetEventIdFromString(event));
	}

	void EventObject::RemoveObservers(unsigned long event, ExecuteCommand* cmd)
	{
		if (this->subjectHelper)
		{
			this->subjectHelper->RemoveObservers(event, cmd);
		}
	}

	void EventObject::RemoveObservers(const char* event, ExecuteCommand* cmd)
	{
		this->RemoveObservers(ExecuteCommand::GetEventIdFromString(event), cmd);
	}

	void EventObject::RemoveAllObservers()
	{
		if (this->subjectHelper)
		{
			this->subjectHelper->RemoveAllObservers();
		}
	}

	bool EventObject::InvokeEvent(unsigned long event, void* callData)
	{
		if (this->subjectHelper)
		{
			return this->subjectHelper->InvokeEvent(event, callData, this);
		}
		return false;
	}

	bool EventObject::InvokeEvent(const char* event, void* callData)
	{
		return this->InvokeEvent(ExecuteCommand::GetEventIdFromString(event), callData);
	}

	bool EventObject::HasObserver(unsigned long event)
	{
		if (this->subjectHelper)
		{
			return this->subjectHelper->HasObserver(event);
		}
		return false;
	}

	bool EventObject::HasObserver(const char* event)
	{
		return this->HasObserver(ExecuteCommand::GetEventIdFromString(event));
	}

	bool EventObject::HasObserver(unsigned long event, ExecuteCommand* cmd)
	{
		if (this->subjectHelper)
		{
			return this->subjectHelper->HasObserver(event, cmd);
		}
		return false;
	}

	bool EventObject::HasObserver(const char* event, ExecuteCommand* cmd)
	{
		return this->HasObserver(ExecuteCommand::GetEventIdFromString(event), cmd);
	}

	void EventObject::InternalGrabFocus(ExecuteCommand* mouseEvents, ExecuteCommand* keypressEvents)
	{
		if (this->subjectHelper)
		{
			this->subjectHelper->GrabFocus(mouseEvents, keypressEvents);
		}
	}


	void EventObject::InternalReleaseFocus()
	{
		if (this->subjectHelper)
		{
			this->subjectHelper->ReleaseFocus();
		}
	}


	void EventObject::ObjectFinalize()
	{
		this->InvokeEvent(ExecuteCommand::DeleteEvent, nullptr);
		this->RemoveAllObservers();
	}

	class ObjectCommandInternal : public ExecuteCommand
	{
		EventObject::ClassMemberCallbackBase* Callable=nullptr;

	public:
		static ObjectCommandInternal* New() { return new ObjectCommandInternal(); }
		void Execute(EventObject* caller, unsigned long eventId, void* callData) override
		{
			if (this->Callable)
			{
				this->AbortFlagOff();
				if ((*this->Callable)(caller, eventId, callData))
				{
					this->AbortFlagOn();
				}
			}
		}

		// Takes over the ownership of \c callable.
		void SetCallable(EventObject::ClassMemberCallbackBase* callable)
		{
			delete this->Callable;
			this->Callable = callable;
		}

	protected:
		ObjectCommandInternal() { this->Callable = nullptr; }
		~ObjectCommandInternal() override { delete this->Callable; }
	};
	ExecuteCommandPair EventObject::AddTemplatedObserver(unsigned long event, ClassMemberCallbackBase* callable, float priority)
	{
		//have memory leaks!
		ObjectCommandInternal* command = ObjectCommandInternal::New();
		command->SetCallable(callable);
		unsigned long id = this->AddObserver(event, command, priority);
		return { id ,command};
	}
}