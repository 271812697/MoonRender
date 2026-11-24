#include <glad/glad.h>

#include <Rendering/HAL/OpenGL/GLRenderbuffer.h>
#include <Rendering/HAL/OpenGL/GLTypes.h>

// The backend object of a renderbuffer: everything here runs where the context is, on the object
// that lives in the handle's slot. The recording side never calls any of it - what a front end
// (Rendering::Data::Renderbuffer) does is record the commands whose executors land here.

template<>
Rendering::HAL::GLRenderbuffer::TRenderbuffer(BackendConstructionTag)
{
	glCreateRenderbuffers(1, &m_context.glId);
}

template<>
void Rendering::HAL::GLRenderbuffer::Bind() const
{
	glBindRenderbuffer(GL_RENDERBUFFER, m_context.glId);
}

template<>
void Rendering::HAL::GLRenderbuffer::Allocate(
	uint16_t p_width,
	uint16_t p_height,
	Settings::EInternalFormat p_format,
	bool p_multiSample) const
{
	const GLenum format = EnumToValue<GLenum>(p_format);
	if (p_multiSample)
	{
		glNamedRenderbufferStorageMultisample(m_context.glId, 4, format, p_width, p_height);
	}
	else
	{
		glNamedRenderbufferStorage(m_context.glId, format, p_width, p_height);
	}
}

template<>
uint32_t Rendering::HAL::GLRenderbuffer::GetID() const
{
	return m_context.glId;
}
