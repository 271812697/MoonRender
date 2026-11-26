#include <Rendering/Data/BufferObject.h>

#include <cstdint>

#include <Rendering/Backend/BackendResources.h>
#include <Rendering/Backend/BufferDescriptor.h>
#include <Rendering/Context/Driver.h>

namespace Rendering::Data
{
	BufferObject::BufferObject(Settings::EBufferType p_type) : m_type(p_type)
	{
		// Filament's createBufferObject: the driver reserves the handle and records the command that
		// constructs the backend object in its slot (createBufferObjectS / createBufferObjectR). The
		// handle is usable for anything recorded after this.
		m_handle = Rendering::Context::Driver::Get().CreateBufferObject(p_type);
	}

	BufferObject::~BufferObject()
	{
		// Teardown does not go through the driver: the slot is freed here and the GL name is queued
		// to the side that owns the context (Backend::Records::DestroyBuffer). That keeps releasing
		// a buffer safe from any thread and after the module - and with it the driver - is gone,
		// which is where the long lived singletons (the overlay, the uniform pool) let theirs go.
		Rendering::Backend::Records::DestroyBuffer(m_handle);
	}

	BufferObject::BufferObject(BufferObject&& p_other) noexcept
		: m_handle(p_other.m_handle)
		, m_type(p_other.m_type)
		, m_size(p_other.m_size)
	{
		p_other.m_handle.clear();
		p_other.m_size = 0;
	}

	BufferObject& BufferObject::operator=(BufferObject&& p_other) noexcept
	{
		if (this != &p_other)
		{
			Rendering::Backend::Records::DestroyBuffer(m_handle);

			m_handle = p_other.m_handle;
			m_type = p_other.m_type;
			m_size = p_other.m_size;

			p_other.m_handle.clear();
			p_other.m_size = 0;
		}
		return *this;
	}

	uint64_t BufferObject::Allocate(uint64_t p_size, Settings::EAccessSpecifier p_usage)
	{
		Rendering::Context::Driver::Get().AllocateBufferObject(m_handle, p_size, p_usage);
		return m_size = p_size;
	}

	void BufferObject::Upload(const void* p_data, std::optional<Rendering::HAL::BufferMemoryRange> p_range)
	{
		const uint64_t offset = p_range.has_value() ? p_range->offset : 0;
		const uint64_t size = p_range.has_value() ? p_range->size : m_size;
		// The bytes are copied into the descriptor, so the command owns them from here on
		// (Filament's BufferDescriptor): the caller's memory may be gone when the batch runs.
		Rendering::Context::Driver::Get().UpdateBufferObject(
			m_handle, Rendering::Backend::BufferDescriptor::Own(p_data, size), offset);
	}

	void BufferObject::Bind(std::optional<uint32_t> p_index) const
	{
		Rendering::Context::Driver::Get().BindBufferObject(m_handle, m_type, p_index);
	}

	void BufferObject::BindRange(uint32_t p_index, uint64_t p_offset, uint64_t p_size) const
	{
		Rendering::Context::Driver::Get().BindBufferObjectRange(m_handle, p_index, p_offset, p_size);
	}

	void BufferObject::Unbind() const
	{
		Rendering::Context::Driver::Get().UnbindBufferObject(m_type);
	}

	void* BufferObject::MapRead(uint64_t p_offset, uint64_t p_size)
	{
		return Rendering::Context::Driver::Get().MapBufferRead(m_handle, p_offset, p_size);
	}

	void BufferObject::Unmap()
	{
		Rendering::Context::Driver::Get().UnmapBufferObject(m_handle);
	}

	void BufferObject::InsertFence()
	{
		Rendering::Context::Driver::Get().InsertBufferFence(m_handle);
	}

	bool BufferObject::IsFenceSignaled() const
	{
		return Rendering::Context::Driver::Get().IsBufferFenceSignaled(m_handle);
	}

	void BufferObject::ClearFence()
	{
		Rendering::Context::Driver::Get().ClearBufferFence(m_handle);
	}
}
