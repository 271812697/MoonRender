#include <glad/glad.h>

#include <Rendering/HAL/OpenGL/GLVertexArray.h>
#include <Rendering/HAL/OpenGL/GLTypes.h>
#include <assert.h>

// The backend object of a vertex array: everything here runs where the context is, on the object
// that lives in the handle's slot. The recording side never calls any of it - what a front end
// (Rendering::Data::VertexArrayObject) does is record the commands whose executors land here.

template<>
Rendering::HAL::GLVertexArray::TVertexArray(BackendConstructionTag)
{
	// The backend half of the create: the slot exists (the recording side reserved it) and the
	// context is current, so the name is made here (Filament's createVertexArrayR).
	glCreateVertexArrays(1, &m_context.glId);
}

template<>
void Rendering::HAL::GLVertexArray::Bind() const
{
	glBindVertexArray(m_context.glId);
}

template<>
void Rendering::HAL::GLVertexArray::Unbind() const
{
	glBindVertexArray(0);
}

template<>
void Rendering::HAL::GLVertexArray::SetLayout(
	const std::vector<Rendering::Backend::VertexLayoutAttribute>& p_attributes,
	uint32_t p_vertexBufferId,
	uint32_t p_indexBufferId) const
{
	glBindVertexArray(m_context.glId);

	// The element buffer binding is part of the array's state; the array buffer binding is not
	// (the attribute pointers capture the buffer they were described against), so both are set
	// while the array is bound, which is where the layout is stored.
	if (p_indexBufferId != 0)
	{
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, p_indexBufferId);
	}
	if (p_vertexBufferId != 0)
	{
		glBindBuffer(GL_ARRAY_BUFFER, p_vertexBufferId);
	}

	for (uint32_t index = 0; index < static_cast<uint32_t>(p_attributes.size()); ++index)
	{
		const Rendering::Backend::VertexLayoutAttribute& attribute = p_attributes[index];
		glEnableVertexAttribArray(index);
		glVertexAttribPointer(index,
			static_cast<GLint>(attribute.count),
			EnumToValue<GLenum>(attribute.type),
			static_cast<GLboolean>(attribute.normalized),
			static_cast<GLsizei>(attribute.stride),
			reinterpret_cast<const void*>(static_cast<uintptr_t>(attribute.offset)));
	}

	glBindVertexArray(0);
}

template<>
uint32_t Rendering::HAL::GLVertexArray::GetID() const
{
	return m_context.glId;
}
