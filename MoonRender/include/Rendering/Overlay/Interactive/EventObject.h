#pragma once

namespace MOON {
	class SubjectHelper;
	class ExecuteCommand;
	struct ExecuteCommandPair
	{
		unsigned long tag;
		ExecuteCommand* command;
	};
	class  EventObject
	{
	public:
		EventObject* New();
		unsigned long AddObserver(unsigned long event, ExecuteCommand*, float priority = 0.0f);
		unsigned long AddObserver(const char* event, ExecuteCommand*, float priority = 0.0f);
		ExecuteCommand* GetExecuteCommand(unsigned long tag);

		void RemoveObserver(ExecuteCommand*);
		void RemoveObservers(unsigned long event, ExecuteCommand*);
		void RemoveObservers(const char* event, ExecuteCommand*);
		bool HasObserver(unsigned long event, ExecuteCommand*);
		bool HasObserver(const char* event, ExecuteCommand*);

		void RemoveObserver(unsigned long tag);
		void RemoveObservers(unsigned long event);
		void RemoveObservers(const char* event);
		void RemoveAllObservers(); // remove every last one of them
		bool HasObserver(unsigned long event);
		bool HasObserver(const char* event);

		bool InvokeEvent(unsigned long event, void* callData);
		bool InvokeEvent(const char* event, void* callData);
		///@}

		bool InvokeEvent(unsigned long event) { return this->InvokeEvent(event, nullptr); }
		bool InvokeEvent(const char* event) { return this->InvokeEvent(event, nullptr); }


		template <class U, class T>
		ExecuteCommandPair AddObserver(
			unsigned long event, U observer, void (T::* callback)(), float priority = 0.0f)
		{
			//have memory leaks!
			ClassMemberCallback<T>* callable = new ClassMemberCallback<T>(observer, callback);
			return this->AddTemplatedObserver(event, callable, priority);
		}
		template <class U, class T>
		ExecuteCommandPair AddObserver(unsigned long event, U observer,
			void (T::* callback)(EventObject*, unsigned long, void*), float priority = 0.0f)
		{
			ClassMemberCallback<T>* callable = new ClassMemberCallback<T>(observer, callback);
			return this->AddTemplatedObserver(event, callable, priority);
		}
		template <class U, class T>
		ExecuteCommandPair AddObserver(unsigned long event, U observer,
			bool (T::* callback)(EventObject*, unsigned long, void*), float priority = 0.0f)
		{
			ClassMemberCallback<T>* callable = new ClassMemberCallback<T>(observer, callback);
			return this->AddTemplatedObserver(event, callable, priority);
		}
	protected:
		EventObject();
		virtual ~EventObject();
		void InternalGrabFocus(ExecuteCommand* mouseEvents, ExecuteCommand* keypressEvents = nullptr);
		void InternalReleaseFocus();

	private:
		void ObjectFinalize();
	protected:
		SubjectHelper* subjectHelper;
	private:

		class ClassMemberCallbackBase
		{
		public:
			virtual bool operator()(EventObject*, unsigned long, void*) = 0;
			virtual ~ClassMemberCallbackBase() = default;
		};
		template <class T>
		class ClassMemberCallback : public ClassMemberCallbackBase
		{
			T* Handler;
			void (T::* Method1)();
			void (T::* Method2)(EventObject*, unsigned long, void*);
			bool (T::* Method3)(EventObject*, unsigned long, void*);

		public:
			ClassMemberCallback(T* handler, void (T::* method)())
			{
				this->Handler = handler;
				this->Method1 = method;
				this->Method2 = nullptr;
				this->Method3 = nullptr;
			}

			ClassMemberCallback(T* handler, void (T::* method)(EventObject*, unsigned long, void*))
			{
				this->Handler = handler;
				this->Method1 = nullptr;
				this->Method2 = method;
				this->Method3 = nullptr;
			}

			ClassMemberCallback(T* handler, bool (T::* method)(EventObject*, unsigned long, void*))
			{
				this->Handler = handler;
				this->Method1 = nullptr;
				this->Method2 = nullptr;
				this->Method3 = method;
			}
			~ClassMemberCallback() override = default;

			// Called when the event is invoked
			bool operator()(EventObject* caller, unsigned long event, void* calldata) override
			{
				T* handler = Handler;
				if (handler)
				{
					if (this->Method1)
					{
						(handler->*this->Method1)();
					}
					else if (this->Method2)
					{
						(handler->*this->Method2)(caller, event, calldata);
					}
					else if (this->Method3)
					{
						return (handler->*this->Method3)(caller, event, calldata);
					}
				}
				return false;
			}
		};

		ExecuteCommandPair AddTemplatedObserver(
			unsigned long event, ClassMemberCallbackBase* callable, float priority);
		friend class ObjectCommandInternal;
	};

}


