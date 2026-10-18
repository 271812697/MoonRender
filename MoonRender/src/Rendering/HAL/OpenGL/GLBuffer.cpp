#include <glad/glad.h>

#include <Rendering/HAL/OpenGL/GLBuffer.h>
#include <Rendering/HAL/OpenGL/GLTypes.h>
#include <assert.h>

template<>
Rendering::HAL::GLBuffer::TBuffer(Settings::EBufferType p_type) : m_buffer{
	.type = p_type
}
{
	glCreateBuffers(1, &m_buffer.id);
}

template<>
Rendering::HAL::GLBuffer::~TBuffer()
{
	glDeleteBuffers(1, &m_buffer.id);
}

template<>
uint64_t Rendering::HAL::GLBuffer::Allocate(uint64_t p_size, Settings::EAccessSpecifier p_usage)
{
	assert(IsValid()&& "Cannot allocate memory for an invalid buffer");
	glNamedBufferData(m_buffer.id, p_size, nullptr, EnumToValue<GLenum>(p_usage));
	return m_buffer.allocatedBytes = p_size;
}

template<>
void Rendering::HAL::GLBuffer::Upload(const void* p_data, std::optional<BufferMemoryRange> p_range)
{
	assert(IsValid()&&"Trying to upload data to an invalid buffer");
	assert(!IsEmpty()&&"Trying to upload data to an empty buffer");

	glNamedBufferSubData(
		m_buffer.id,
		p_range ? p_range->offset : 0,
		p_range ? p_range->size : m_buffer.allocatedBytes,
		p_data
	);
}

template<>
void Rendering::HAL::GLBuffer::Bind(std::optional<uint32_t> p_index) const
{
	assert(IsValid()&& "Cannot bind an invalid buffer");

	if (p_index.has_value())
	{
		glBindBufferBase(EnumToValue<GLenum>(m_buffer.type), p_index.value(), m_buffer.id);
	}
	else
	{
		glBindBuffer(EnumToValue<GLenum>(m_buffer.type), m_buffer.id);
	}
}

template<>
void Rendering::HAL::GLBuffer::BindRange(uint32_t p_index, uint64_t p_offset, uint64_t p_size) const
{
	assert(IsValid()&& "Cannot bind an invalid buffer");
	glBindBufferRange(EnumToValue<GLenum>(m_buffer.type), p_index, m_buffer.id, p_offset, p_size);
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
	return
		m_buffer.id != 0 &&
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
	return m_buffer.id;
}

template<>
void* Rendering::HAL::GLBuffer::MapRead(uint64_t p_offset, uint64_t p_size)
{
	assert(IsValid() && "Cannot map an invalid buffer");
	assert(!IsEmpty() && "Cannot map an empty buffer");

	return glMapNamedBufferRange(
		m_buffer.id,
		p_offset,
		p_size,
		GL_MAP_READ_BIT
	);
}

template<>
void Rendering::HAL::GLBuffer::Unmap()
{
	assert(IsValid() && "Cannot unmap an invalid buffer");
	glUnmapNamedBuffer(m_buffer.id);
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
