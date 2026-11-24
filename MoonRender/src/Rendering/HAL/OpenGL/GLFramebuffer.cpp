#include <glad/glad.h>

#include <Rendering/HAL/OpenGL/GLFramebuffer.h>
#include <Rendering/HAL/OpenGL/GLTypes.h>

// The backend object of a render target: everything here runs where the context is, on the object
// that lives in the handle's slot. The recording side never calls any of it - what a front end
// (Rendering::Data::Framebuffer) does is record the commands whose executors land here.

namespace
{
	GLenum AttachmentToGL(Rendering::Settings::EFramebufferAttachment p_attachment, uint32_t p_index)
	{
		return Rendering::HAL::EnumToValue<GLenum>(p_attachment) + static_cast<GLenum>(p_index);
	}
}

template<>
Rendering::HAL::GLFramebuffer::TFramebuffer(BackendConstructionTag)
{
	// The backend half of the create: the slot exists (the recording side reserved it) and the
	// context is current, so the name is made here (Filament's createRenderTargetR).
	glCreateFramebuffers(1, &m_context.glId);
}

template<>
void Rendering::HAL::GLFramebuffer::Bind() const
{
	glBindFramebuffer(GL_FRAMEBUFFER, m_context.glId);
}

template<>
void Rendering::HAL::GLFramebuffer::Unbind() const
{
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

template<>
void Rendering::HAL::GLFramebuffer::AttachTexture(
	Settings::EFramebufferAttachment p_attachment,
	uint32_t p_index,
	uint32_t p_textureId,
	uint32_t p_level) const
{
	glNamedFramebufferTexture(m_context.glId, AttachmentToGL(p_attachment, p_index), p_textureId,
		static_cast<GLint>(p_level));
}

template<>
void Rendering::HAL::GLFramebuffer::AttachTextureLayer(
	Settings::EFramebufferAttachment p_attachment,
	uint32_t p_index,
	uint32_t p_textureId,
	uint32_t p_level,
	uint32_t p_layer) const
{
	glNamedFramebufferTextureLayer(m_context.glId, AttachmentToGL(p_attachment, p_index), p_textureId,
		static_cast<GLint>(p_level), static_cast<GLint>(p_layer));
}

template<>
void Rendering::HAL::GLFramebuffer::AttachRenderbuffer(
	Settings::EFramebufferAttachment p_attachment,
	uint32_t p_index,
	uint32_t p_renderbufferId) const
{
	glNamedFramebufferRenderbuffer(m_context.glId, AttachmentToGL(p_attachment, p_index),
		GL_RENDERBUFFER, p_renderbufferId);
}

template<>
void Rendering::HAL::GLFramebuffer::SetDrawBuffers(const std::vector<uint32_t>& p_indices) const
{
	if (p_indices.empty())
	{
		// No colour attachment writes: the single-buffer form of the same call.
		glNamedFramebufferDrawBuffer(m_context.glId, GL_NONE);
		return;
	}

	std::vector<GLenum> buffers;
	buffers.reserve(p_indices.size());
	for (const uint32_t index : p_indices)
	{
		buffers.push_back(GL_COLOR_ATTACHMENT0 + index);
	}
	glNamedFramebufferDrawBuffers(m_context.glId, static_cast<GLsizei>(buffers.size()), buffers.data());
}

template<>
void Rendering::HAL::GLFramebuffer::SetDrawBuffer(std::optional<uint32_t> p_index) const
{
	glNamedFramebufferDrawBuffer(m_context.glId,
		p_index.has_value() ? GL_COLOR_ATTACHMENT0 + p_index.value() : GL_NONE);
}

template<>
void Rendering::HAL::GLFramebuffer::SetReadBuffer(std::optional<uint32_t> p_index) const
{
	glNamedFramebufferReadBuffer(m_context.glId,
		p_index.has_value() ? GL_COLOR_ATTACHMENT0 + p_index.value() : GL_NONE);
}

template<>
bool Rendering::HAL::GLFramebuffer::CheckStatus() const
{
	return glCheckNamedFramebufferStatus(m_context.glId, GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
}

template<>
uint32_t Rendering::HAL::GLFramebuffer::GetStatus() const
{
	return static_cast<uint32_t>(glCheckNamedFramebufferStatus(m_context.glId, GL_FRAMEBUFFER));
}

template<>
void Rendering::HAL::GLFramebuffer::BlitTo(
	uint32_t p_targetId,
	uint16_t p_srcWidth,
	uint16_t p_srcHeight,
	uint16_t p_dstWidth,
	uint16_t p_dstHeight) const
{
	glBlitNamedFramebuffer(m_context.glId, p_targetId,
		0, 0, p_srcWidth, p_srcHeight,
		0, 0, p_dstWidth, p_dstHeight,
		GL_COLOR_BUFFER_BIT, GL_LINEAR);
}

template<>
void Rendering::HAL::GLFramebuffer::Clear(Settings::EFramebufferAttachment p_attachment, int p_index) const
{
	const GLfloat clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	const GLfloat clearDepth = 1.0f;
	const GLint clearStencil = 0;

	switch (p_attachment)
	{
	case Settings::EFramebufferAttachment::COLOR:
		glClearNamedFramebufferfv(m_context.glId, GL_COLOR, p_index, clearColor);
		break;
	case Settings::EFramebufferAttachment::DEPTH:
		glClearNamedFramebufferfv(m_context.glId, GL_DEPTH, 0, &clearDepth);
		break;
	case Settings::EFramebufferAttachment::STENCIL:
		glClearNamedFramebufferiv(m_context.glId, GL_STENCIL, 0, &clearStencil);
		break;
	default:
		break;
	}
}

template<>
void Rendering::HAL::GLFramebuffer::ReadPixels(
	uint32_t p_x,
	uint32_t p_y,
	uint32_t p_width,
	uint32_t p_height,
	Settings::EPixelDataFormat p_format,
	Settings::EPixelDataType p_type,
	void* p_data) const
{
	glBindFramebuffer(GL_FRAMEBUFFER, m_context.glId);
	glReadPixels(static_cast<GLint>(p_x), static_cast<GLint>(p_y),
		static_cast<GLsizei>(p_width), static_cast<GLsizei>(p_height),
		EnumToValue<GLenum>(p_format), EnumToValue<GLenum>(p_type), p_data);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

template<>
void Rendering::HAL::GLFramebuffer::ReadPixelsToBuffer(
	uint32_t p_pixelPackBufferId,
	uint64_t p_offset,
	uint32_t p_x,
	uint32_t p_y,
	uint32_t p_width,
	uint32_t p_height,
	Settings::EPixelDataFormat p_format,
	Settings::EPixelDataType p_type) const
{
	glBindFramebuffer(GL_FRAMEBUFFER, m_context.glId);
	glBindBuffer(GL_PIXEL_PACK_BUFFER, p_pixelPackBufferId);
	glReadPixels(static_cast<GLint>(p_x), static_cast<GLint>(p_y),
		static_cast<GLsizei>(p_width), static_cast<GLsizei>(p_height),
		EnumToValue<GLenum>(p_format), EnumToValue<GLenum>(p_type),
		reinterpret_cast<void*>(static_cast<uintptr_t>(p_offset)));
	glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

template<>
uint32_t Rendering::HAL::GLFramebuffer::GetID() const
{
	return m_context.glId;
}
