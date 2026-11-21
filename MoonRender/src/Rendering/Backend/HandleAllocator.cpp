#include <Rendering/Backend/HandleAllocator.h>

bool Rendering::Backend::HandleAllocator::IsValid(ResourceHandleBase p_handle) const
{
	std::lock_guard lock(m_mutex);
	const Slot* const slot = FindSlot(p_handle.GetId());
	return slot != nullptr && slot->object != nullptr;
}

size_t Rendering::Backend::HandleAllocator::SlotCount() const
{
	std::lock_guard lock(m_mutex);
	return m_slots.size();
}

Rendering::Backend::HandleAllocator& Rendering::Backend::GetHandleAllocator()
{
	// Never destroyed: resources are released from static destructors and from the module's
	// closing path, where the allocator's own teardown order is not defined. Leaking one allocator
	// at exit is the cheap side of that (the same call DeferredDeleter makes for its state).
	static HandleAllocator* const allocator = new HandleAllocator();
	return *allocator;
}
