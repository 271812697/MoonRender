#include <glad/glad.h>

#include <Rendering/Backend/BackendResources.h>
#include <Rendering/Backend/BufferRequests.h>
#include <Rendering/Context/Driver.h>
#include <Rendering/HAL/Common/HALDriverAccess.h>
#include <Rendering/HAL/Common/ResourceOps.h>
#include <Rendering/HAL/OpenGL/GLBuffer.h>
#include <Rendering/HAL/OpenGL/GLTypes.h>
#include <assert.h>

template<>
Rendering::HAL::GLBuffer::TBuffer(Settings::EBufferType p_type) : m_buffer{
	.type = p_type
}
{
	// Filament's createBufferObjectS/createBufferObjectR split: the slot is reserved here (no GL),
	// and the name is made on the side that owns the context (see ResourceOps::EnsureBuffer).
	m_buffer.handle = ::Rendering::Backend::GetHandleAllocator().Allocate<::Rendering::Backend::HwBufferObject>();
	::Rendering::HAL::ResourceOps::EnsureBuffer(m_buffer.handle);
}

template<>
Rendering::HAL::GLBuffer::~TBuffer()
{
	// Deletion is GL too: the record is freed now and the name is queued to the context side.
	if (m_buffer.handle)
	{
		::Rendering::HAL::ResourceOps::DestroyBuffer(m_buffer.handle);
		m_buffer.handle.clear();
	}
}
template<>
uint64_t Rendering::HAL::GLBuffer::Allocate(uint64_t p_size, Settings::EAccessSpecifier p_usage)
{
	assert(IsValid()&& "Cannot allocate memory for an invalid buffer");

	// The name is made on the side that owns the context: recorded when that side is another
	// thread, done here otherwise (see ResourceOps).
	::Rendering::HAL::ResourceOps::AllocateBuffer(m_buffer.handle, p_size, p_usage);
	return m_buffer.allocatedBytes = p_size;
}

template<>
void Rendering::HAL::GLBuffer::Upload(const void* p_data, std::optional<BufferMemoryRange> p_range)
{
	assert(IsValid()&&"Trying to upload data to an invalid buffer");
	assert(!IsEmpty()&&"Trying to upload data to an empty buffer");

	// An upload is a command, like every other operation that needs the context (Filament's
	// updateBufferObject): the bytes are copied into the command and written by the executor. The
	// ring is sized for one batch (Filament's minCommandBufferSizeMB), so no flush can land
	// between the bytes and the command any more.
	::Rendering::HAL::ResourceOps::UploadBuffer(
		m_buffer.handle,
		p_data,
		p_range ? p_range->offset : 0,
		p_range ? p_range->size : m_buffer.allocatedBytes);
}

template<>
void Rendering::HAL::GLBuffer::Bind(std::optional<uint32_t> p_index) const
{
	assert(IsValid()&& "Cannot bind an invalid buffer");

	// A binding point only means something on the side that owns the context: record it, so the
	// executor binds it before the draws that read it run. Binding it here left the light SSBO
	// unbound on the render thread and every lit surface read an empty light list (docs §8.67).
	// Recorded in both configurations for the same reason (§8.72).
	if (::Rendering::Context::Driver* const driver = ::Rendering::HAL::CurrentDriver();
		driver != nullptr)
	{
		::Rendering::Backend::BindBufferRequest request;
		request.handleId = m_buffer.handle.GetId();
		request.type = m_buffer.type;
		request.indexed = p_index.has_value();
		request.index = p_index.value_or(0);
		driver->EnqueueBindBuffer(request);
		return;
	}

	if (p_index.has_value())
	{
		glBindBufferBase(EnumToValue<GLenum>(m_buffer.type), p_index.value(), GetID());
	}
	else
	{
		glBindBuffer(EnumToValue<GLenum>(m_buffer.type), GetID());
	}
}

template<>
void Rendering::HAL::GLBuffer::BindRange(uint32_t p_index, uint64_t p_offset, uint64_t p_size) const
{
	assert(IsValid()&& "Cannot bind an invalid buffer");
	glBindBufferRange(EnumToValue<GLenum>(m_buffer.type), p_index, GetID(), p_offset, p_size);
}

template<>
void Rendering::HAL::GLBuffer::Unbind() const
{
	assert(IsValid()&&"Cannot unbind an invalid buffer");
	glBindBuffer(EnumToValue<GLenum>(m_buffer.type), 0);
}

template<>
bool Rendering::HAL::GLBuffer::IsValid() const
{
	// Only what this side owns: the handle that names the object and the kind of buffer it is.
	// Whether the backend object has its GL name yet is the executor's business - it is made by a
	// command, so on this side the name may legitimately not exist (Filament's BufferObject has the
	// same split: the handle is the front end's, gl.id the backend's).
	return
		static_cast<bool>(m_buffer.handle) &&
		m_buffer.type != Settings::EBufferType::UNKNOWN;
}

template<>
bool Rendering::HAL::GLBuffer::IsEmpty() const
{
	return GetSize() == 0;
}

template<>
uint64_t Rendering::HAL::GLBuffer::GetSize() const
{
	assert(IsValid()&&"Cannot get size of an invalid buffer");
	return m_buffer.allocatedBytes;
}

template<>
uint32_t Rendering::HAL::GLBuffer::GetID() const
{
	assert(IsValid()&&"Cannot get ID of an invalid buffer");
	return ::Rendering::Backend::Records::BufferName(m_buffer.handle);
}

template<>
void* Rendering::HAL::GLBuffer::MapRead(uint64_t p_offset, uint64_t p_size)
{
	assert(IsValid() && "Cannot map an invalid buffer");
	assert(!IsEmpty() && "Cannot map an empty buffer");

	return glMapNamedBufferRange(
		GetID(),
		p_offset,
		p_size,
		GL_MAP_READ_BIT
	);
}

template<>
void Rendering::HAL::GLBuffer::Unmap()
{
	assert(IsValid() && "Cannot unmap an invalid buffer");
	glUnmapNamedBuffer(GetID());
}

template<>
void Rendering::HAL::GLBuffer::InsertFence()
{
	ClearFence();
	assert(IsValid() && "Cannot fence an invalid buffer");
	m_buffer.fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
}

template<>
bool Rendering::HAL::GLBuffer::IsFenceSignaled() const
{
	if (m_buffer.fence == nullptr)
	{
		return true;
	}

	const GLenum result = glClientWaitSync(
		static_cast<GLsync>(m_buffer.fence),
		GL_SYNC_FLUSH_COMMANDS_BIT,
		0
	);

	return result == GL_ALREADY_SIGNALED || result == GL_CONDITION_SATISFIED;
}

template<>
void Rendering::HAL::GLBuffer::ClearFence()
{
	if (m_buffer.fence != nullptr)
	{
		glDeleteSync(static_cast<GLsync>(m_buffer.fence));
		m_buffer.fence = nullptr;
	}
}
