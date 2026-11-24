#include <Rendering/Data/BufferObject.h>

#include <Rendering/Backend/BackendResources.h>
#include <Rendering/Backend/HandleAllocator.h>
#include <Rendering/HAL/Common/ResourceOps.h>

namespace Rendering::Data
{
	BufferObject::BufferObject(Settings::EBufferType p_type) : m_type(p_type)
	{
		// Filament's createBufferObjectS: the slot is reserved here, without GL, and the create
		// command gives it its backend object on the side that owns the context (see
		// ResourceOps::EnsureBuffer). The handle is usable for anything recorded after this.
		m_handle = Rendering::Backend::GetHandleAllocator()
			.Allocate<Rendering::Backend::HwBufferObject>();

		Rendering::HAL::ResourceOps::EnsureBuffer(m_handle, m_type);
	}

	BufferObject::~BufferObject()
	{
		// The record goes now, the GL name is queued to the side that owns the context (see
		// Records::DestroyBuffer): that side is not necessarily the thread dropping this object - a
		// loader, a feature rebuild on a job thread, closing a document.
		Rendering::HAL::ResourceOps::DestroyBuffer(m_handle);
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
			Rendering::HAL::ResourceOps::DestroyBuffer(m_handle);

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
		// Re-specifying the storage discards what it held, which is what the pool (grow and
		// re-upload) and the overlay (grow, then upload the frame) rely on.
		Rendering::HAL::ResourceOps::AllocateBuffer(m_handle, p_size, p_usage);
		return m_size = p_size;
	}

	void BufferObject::Upload(const void* p_data, std::optional<Rendering::HAL::BufferMemoryRange> p_range)
	{
		Rendering::HAL::ResourceOps::UploadBuffer(
			m_handle,
			p_data,
			p_range ? p_range->offset : 0,
			p_range ? p_range->size : m_size);
	}

	void BufferObject::Bind(std::optional<uint32_t> p_index) const
	{
		Rendering::HAL::ResourceOps::BindBuffer(m_handle, m_type, p_index);
	}

	void BufferObject::BindRange(uint32_t p_index, uint64_t p_offset, uint64_t p_size) const
	{
		Rendering::HAL::ResourceOps::BindBufferRange(m_handle, p_index, p_offset, p_size);
	}

	void BufferObject::Unbind() const
	{
		Rendering::HAL::ResourceOps::UnbindBuffer(m_type);
	}

	void* BufferObject::MapRead(uint64_t p_offset, uint64_t p_size)
	{
		return Rendering::HAL::ResourceOps::MapRead(m_handle, p_offset, p_size);
	}

	void BufferObject::Unmap()
	{
		Rendering::HAL::ResourceOps::UnmapBuffer(m_handle);
	}

	void BufferObject::InsertFence()
	{
		Rendering::HAL::ResourceOps::InsertFence(m_handle);
	}

	bool BufferObject::IsFenceSignaled() const
	{
		return Rendering::HAL::ResourceOps::IsFenceSignaled(m_handle);
	}

	void BufferObject::ClearFence()
	{
		Rendering::HAL::ResourceOps::ClearFence(m_handle);
	}
}
