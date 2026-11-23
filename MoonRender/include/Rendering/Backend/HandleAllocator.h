#pragma once
#include <Rendering/Backend/ResourceHandle.h>

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace Rendering::Backend
{
	/** The one allocator of backend resources, in the shape of Filament's
	 * HandleAllocator (filament/backend/include/private/backend/HandleAllocator.h).
	 *
	 * Why one allocator and not one table per resource type: a handle is an index into a single id
	 * space plus an age, and the backend object it names lives *in the slot*. That is what lets
	 * the front end refer to a resource that does not exist yet - the recording side calls
	 * Allocate() and gets the handle (Filament's createXxxS, no GL), the side that owns the
	 * context calls Construct() and builds the object in that slot (createXxxR), and everything
	 * that needs the object asks HandleCast(). A table per type would have to repeat that
	 * allocate/construct/resolve/release shape once per type, which is exactly what this replaces.
	 *
	 * The age is what makes a released handle safe to keep: freeing a slot bumps its age, so an
	 * old handle no longer resolves to whatever is put in the slot next (use-after-free is a null
	 * HandleCast, not a dangling object). That matters here because handles travel through the
	 * command ring and are resolved later, on another thread.
	 *
	 * Two deliberate differences from Filament:
	 *  - storage is one heap allocation per slot, not an arena with three size pools. The arena
	 *    there exists for allocation speed on mobile; the contract - handle_cast to the object,
	 *    destruction runs on the side that owns the context - is the same.
	 *  - the index is encoded +1, so id 0 keeps meaning "no handle": this engine passes raw ids
	 *    through the command requests and several of them test `== 0`. Filament never does that,
	 *    which is why it can use UINT32_MAX alone.
	 */
	class HandleAllocator
	{
	public:
		HandleAllocator() = default;
		HandleAllocator(HandleAllocator const&) = delete;
		HandleAllocator& operator=(HandleAllocator const&) = delete;

		/** Reserves a slot and returns its handle, constructing nothing (createXxxS). */
		template <typename D>
		ResourceHandle<D> Allocate()
		{
			std::lock_guard lock(m_mutex);
			return ResourceHandle<D>{ReserveSlot()};
		}

		/** Reserves a slot and constructs the object in it (both steps where no GL is involved). */
		template <typename D, typename... TArgs>
		ResourceHandle<D> AllocateAndConstruct(TArgs&&... p_args)
		{
			const ResourceHandle<D> handle = Allocate<D>();
			if (Construct<D>(handle, std::forward<TArgs>(p_args)...) == nullptr)
			{
				Deallocate(handle);
				return ResourceHandle<D>{};
			}
			return handle;
		}

		/** Constructs D in the slot p_handle names (Filament's construct<>, and its
		 * destroyAndConstruct<> when the slot is already occupied). Returns null for a stale
		 * handle. Must run where the context is when D owns GL objects. */
		template <typename D, typename B, typename... TArgs>
		D* Construct(ResourceHandle<B> p_handle, TArgs&&... p_args)
		{
			static_assert(std::is_base_of_v<B, D>);
			static_assert(alignof(D) <= alignof(std::max_align_t),
				"HandleAllocator allocates with plain operator new; over-aligned records need an aligned path");

			std::lock_guard lock(m_mutex);
			Slot* const slot = FindSlot(p_handle.GetId());
			if (slot == nullptr)
			{
				return nullptr;
			}

			// Re-constructing in place: run the old destructor first, as Filament's
			// destroyAndConstruct does.
			if (slot->object != nullptr && slot->destroy != nullptr)
			{
				slot->destroy(slot->object);
				slot->object = nullptr;
				slot->destroy = nullptr;
			}

			D* const object = new (::operator new(sizeof(D))) D(std::forward<TArgs>(p_args)...);
			slot->object = object;
			slot->destroy = [](void* p_object) noexcept
			{
				static_cast<D*>(p_object)->~D();
				::operator delete(p_object);
			};
			return object;
		}

		/** Runs the object's destructor and frees the slot. For GL resources this has to happen on
		 * the side that owns the context, which is why callers queue it (DeferredDeleter) rather
		 * than calling it here from anywhere. */
		template <typename D>
		void Deallocate(ResourceHandle<D> p_handle)
		{
			DeallocateId(p_handle.GetId());
		}

		/** The object at p_handle, or null when the handle is stale (handle_cast). */
		template <typename D, typename B = D>
		D* HandleCast(ResourceHandle<B> p_handle) const
		{
			static_assert(std::is_base_of_v<B, D>);
			std::lock_guard lock(m_mutex);
			const Slot* const slot = FindSlot(p_handle.GetId());
			return slot != nullptr ? static_cast<D*>(slot->object) : nullptr;
		}

		/** Whether p_handle names a live slot that has an object in it. */
		bool IsValid(ResourceHandleBase p_handle) const;

		/** Number of slots ever reserved; diagnostics only. */
		size_t SlotCount() const;

	private:
		struct Slot
		{
			void* object = nullptr;
			/** Destruction of the object, type-erased so that a handle is enough to free the slot
			 * (the same reason Filament's deallocate is templated on D: someone knows the type). */
			void (*destroy)(void*) noexcept = nullptr;
			uint8_t age = 0;
		};

		// Handle layout: 27 bits of index (stored +1), 4 bits of age above them. Filament has the
		// same split, with the index being an arena offset and the top bit marking heap handles.
		static constexpr uint32_t kIndexMask = 0x07FFFFFFu;
		static constexpr uint32_t kAgeShift = 27;
		static constexpr uint32_t kAgeMask = 0xFu << kAgeShift;
		static constexpr uint8_t kMaxAge = 0xF;

		static bool IsHandleId(ResourceHandleBase::HandleId p_id) noexcept
		{
			return p_id != 0 && p_id != ResourceHandleBase::nullid;
		}

		static uint32_t MakeId(size_t p_index, uint8_t p_age) noexcept
		{
			return (static_cast<uint32_t>(p_index) + 1u) | (static_cast<uint32_t>(p_age & kMaxAge) << kAgeShift);
		}

		static size_t IndexOf(ResourceHandleBase::HandleId p_id) noexcept
		{
			return static_cast<size_t>((p_id & kIndexMask) - 1u);
		}

		static uint8_t AgeOf(ResourceHandleBase::HandleId p_id) noexcept
		{
			return static_cast<uint8_t>((p_id & kAgeMask) >> kAgeShift);
		}

		ResourceHandleBase::HandleId ReserveSlot()
		{
			size_t index = 0;
			if (!m_freeSlots.empty())
			{
				index = m_freeSlots.back();
				m_freeSlots.pop_back();
			}
			else
			{
				index = m_slots.size();
				m_slots.emplace_back();
			}

			Slot& slot = m_slots[index];
			slot.object = nullptr;
			slot.destroy = nullptr;
			// The caller wants a plain handle; the typed wrapper is built by Allocate<D>.
			return MakeId(index, slot.age);
		}

		/** The slot p_id names, or null when the id is stale (age mismatch), out of range, or not
		 * a handle at all. */
		Slot* FindSlot(ResourceHandleBase::HandleId p_id)
		{
			if (!IsHandleId(p_id))
			{
				return nullptr;
			}
			const size_t index = IndexOf(p_id);
			if (index >= m_slots.size())
			{
				return nullptr;
			}
			Slot& slot = m_slots[index];
			return AgeOf(p_id) == slot.age ? &slot : nullptr;
		}

		const Slot* FindSlot(ResourceHandleBase::HandleId p_id) const
		{
			return const_cast<HandleAllocator*>(this)->FindSlot(p_id);
		}

		void DeallocateId(ResourceHandleBase::HandleId p_id)
		{
			{
				std::lock_guard lock(m_mutex);
				Slot* const slot = FindSlot(p_id);
				if (slot == nullptr)
				{
					return;
				}

				// The destructor runs with the lock held: it is short (GL deletes), and the
				// recursive mutex keeps a destructor that allocates a handle from deadlocking.
				if (slot->object != nullptr && slot->destroy != nullptr)
				{
					slot->destroy(slot->object);
				}
				slot->object = nullptr;
				slot->destroy = nullptr;
				slot->age = static_cast<uint8_t>((slot->age + 1u) & kMaxAge);
				m_freeSlots.push_back(IndexOf(p_id));
			}
		}

		/** Recursive because an object's destructor can run engine code (HAL::Texture fires an
		 * event), which is allowed to allocate a handle. */
		mutable std::recursive_mutex m_mutex;
		std::vector<Slot> m_slots;
		std::vector<size_t> m_freeSlots;
	};

	/** The process-wide allocator, in the role DriverBase::mHandleAllocator plays in Filament.
	 *
	 * It is never destroyed: resources are released from the closing paths of the module and of
	 * static objects, and owning one allocator per driver would mean threading it through the HAL
	 * layer first. When the driver owns it, this becomes a lookup instead of a static. */
	HandleAllocator& GetHandleAllocator();
}
