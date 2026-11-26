#include <glad/glad.h>

#include <Rendering/HAL/OpenGL/GLBuffer.h>
#include <Rendering/HAL/OpenGL/GLTypes.h>
#include <assert.h>

// The backend object of a buffer: everything here runs where the context is, on the object that
// lives in the handle's slot. The recording side never calls any of it - what a front end
// (Rendering::Data::BufferObject) does is record the command whose executor lands here.

template<>
Rendering::HAL::GLBuffer::TBuffer(Settings::EBufferType p_type, BackendConstructionTag)
	: Rendering::Backend::HwBufferObject(p_type)
{
	// The backend half of the create: the slot exists (the recording side reserved it) and the
	// context is current, so the name is made here (Filament's createBufferObjectR).
	glCreateBuffers(1, &m_buffer.glId);
}

template<>
uint64_t Rendering::HAL::GLBuffer::Allocate(uint64_t p_size, Settings::EAccessSpecifier p_usage)
{
	assert(IsValid() && "Cannot allocate memory for an invalid buffer");

	// Re-specifying the storage discards what it held - what the pool and the overlay's growing
	// buffers rely on (Filament's allocateBufferObject).
	glNamedBufferData(m_buffer.glId, p_size, nullptr, EnumToValue<GLenum>(p_usage));

	return size = p_size;
}

template<>
void Rendering::HAL::GLBuffer::Upload(const void* p_data, std::optional<BufferMemoryRange> p_range)
{
	assert(IsValid() && "Trying to upload data to an invalid buffer");
	assert(!IsEmpty() && "Trying to upload data to an empty buffer");

	glNamedBufferSubData(
		m_buffer.glId,
		p_range ? p_range->offset : 0,
		p_range ? p_range->size : GetSize(),
		p_data);
}

template<>
void Rendering::HAL::GLBuffer::Bind(std::optional<uint32_t> p_index) const
{
	assert(IsValid() && "Cannot bind an invalid buffer");

	const GLenum target = EnumToValue<GLenum>(type);

	if (p_index.has_value())
	{
		glBindBufferBase(target, p_index.value(), m_buffer.glId);
	}
	else
	{
		glBindBuffer(target, m_buffer.glId);
	}
}

template<>
void Rendering::HAL::GLBuffer::BindRange(uint32_t p_index, uint64_t p_offset, uint64_t p_size) const
{
	assert(IsValid() && "Cannot bind an invalid buffer");
	glBindBufferRange(EnumToValue<GLenum>(type), p_index, m_buffer.glId, p_offset, p_size);
}

template<>
void Rendering::HAL::GLBuffer::Unbind() const
{
	assert(IsValid() && "Cannot unbind an invalid buffer");
	glBindBuffer(EnumToValue<GLenum>(type), 0);
}

template<>
bool Rendering::HAL::GLBuffer::IsValid() const
{
	// The name is made by the constructor, on the side that owns the context: an object in a slot
	// always has one.
	return m_buffer.glId != 0;
}

template<>
uint32_t Rendering::HAL::GLBuffer::GetID() const
{
	return m_buffer.glId;
}

template<>
void* Rendering::HAL::GLBuffer::GetFence() const
{
	return m_buffer.fence;
}

template<>
void Rendering::HAL::GLBuffer::Unmap()
{
	assert(IsValid() && "Cannot unmap an invalid buffer");
	glUnmapNamedBuffer(m_buffer.glId);
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
