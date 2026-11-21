#include <Rendering/Backend/DeferredDeleter.h>

#include <mutex>
#include <vector>

namespace Rendering::Backend
{
	namespace
	{
		/** The queue and its lock live in a function-local static that is never destroyed.
		 *
		 * HAL objects are destroyed at static-destruction time as often as not (a renderer that is a
		 * file-scope object, a view owned by a widget), and they push their deletion here. A
		 * namespace-scope queue may already be gone by then, and pushing into it is a
		 * use-after-free - which is what the shutdown path showed. Leaking the queue at exit is the
		 * cheap side of that trade: nothing GL is deleted after the context is gone anyway. */
		struct State
		{
			std::mutex lock;
			std::vector<std::function<void()>> pending;
			bool active = false;
			bool uploadsDeferrable = false;
		};

		State& Get()
		{
			static State* state = new State();
			return *state;
		}
	}

	void DeferredDeleter::Push(std::function<void()> p_work)
	{
		State& state = Get();
		std::lock_guard lock(state.lock);
		state.pending.push_back(std::move(p_work));
	}

	void DeferredDeleter::SetActive(bool p_active)
	{
		State& state = Get();
		std::lock_guard lock(state.lock);
		state.active = p_active;
	}

	void DeferredDeleter::SetUploadsDeferrable(bool p_deferrable)
	{
		Get().uploadsDeferrable = p_deferrable;
	}

	bool DeferredDeleter::AreUploadsDeferrable()
	{
		return Get().uploadsDeferrable;
	}

	bool DeferredDeleter::IsActive()
	{
		State& state = Get();
		std::lock_guard lock(state.lock);
		return state.active;
	}

	void DeferredDeleter::Flush()
	{
		std::vector<std::function<void()>> work;
		State& state = Get();
		{
			std::lock_guard lock(state.lock);
			if (state.pending.empty())
			{
				return;
			}
			work.swap(state.pending);
		}

		// Outside the lock: a destructor may release something that queues more work (a model
		// holding meshes, say), and that has to be able to take the lock.
		for (std::function<void()>& item : work)
		{
			item();
		}
	}
}
