#include <Rendering/View/FramePaster.h>

#include <glad/glad.h>

Rendering::View::FramePaster::~FramePaster()
{
	// The object belongs to the context that was current when it was created; whoever destroys a
	// paster has to do it with that context current (a window does it while tearing its context
	// down). Deleting it from another context would either be a no-op or delete a foreign name.
	Release();
}

void Rendering::View::FramePaster::Release()
{
	if (m_readFramebuffer != 0)
	{
		glDeleteFramebuffers(1, &m_readFramebuffer);
		m_readFramebuffer = 0;
	}
}

void Rendering::View::FramePaster::Paste(
	uint32_t p_sourceTexture, int p_sourceWidth, int p_sourceHeight,
	uint32_t p_destinationFramebuffer, int p_destinationWidth, int p_destinationHeight)
{
	if (p_sourceTexture == 0 || p_sourceWidth <= 0 || p_sourceHeight <= 0 ||
		p_destinationWidth <= 0 || p_destinationHeight <= 0)
	{
		return;
	}

	if (m_readFramebuffer == 0)
	{
		glGenFramebuffers(1, &m_readFramebuffer);
	}

	// A blit needs the source attached to a framebuffer of *this* context: the texture belongs
	// to the render module's context, and textures are shared while framebuffer objects are not.
	glBindFramebuffer(GL_READ_FRAMEBUFFER, m_readFramebuffer);
	glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, p_sourceTexture, 0);

	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, p_destinationFramebuffer);
	glBlitFramebuffer(0, 0, p_sourceWidth, p_sourceHeight,
		0, 0, p_destinationWidth, p_destinationHeight,
		GL_COLOR_BUFFER_BIT, GL_LINEAR);

	// Leave the context on the destination with nothing foreign still attached: the next frame
	// binds its own targets, but a stale read attachment is exactly the kind of state that makes
	// a later paste use the wrong texture.
	glBindFramebuffer(GL_READ_FRAMEBUFFER, m_readFramebuffer);
	glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, p_destinationFramebuffer);
}
