#include <glad/glad.h>

#include <Rendering/Backend/BackendResources.h>
#include <Rendering/Backend/FramebufferRequests.h>
#include <Rendering/Context/Driver.h>
#include <Rendering/HAL/Common/HALDriverAccess.h>
#include <Rendering/HAL/OpenGL/GLRenderbuffer.h>
#include <Rendering/HAL/OpenGL/GLTypes.h>

template<>
Rendering::HAL::GLRenderbuffer::TRenderbuffer(bool flag)
{
	// createRenderbufferS/createRenderbufferR: the handle here, the name where the context is. The
	// create is synchronous for the caller and the same with or without a render thread
	// (docs/RenderThreadPlan.md §8.72).
	m_context.handle = ::Rendering::Backend::GetHandleAllocator().Allocate<::Rendering::Backend::HwRenderbuffer>();
	::Rendering::Context::Driver* const driver = ::Rendering::HAL::CurrentDriver();
	if (driver != nullptr)
	{
		driver->EnqueueCreateRenderbuffer(m_context.handle.GetId());
		driver->ExecutePendingCommands();
	}
	else
	{
		::Rendering::Backend::Records::CreateRenderbuffer(m_context.handle);
	}
	isMultisample = flag;
}

template<>
Rendering::HAL::GLRenderbuffer::~TRenderbuffer()
{
	if (m_context.handle)
	{
		::Rendering::Backend::Records::DestroyRenderbuffer(m_context.handle);
		m_context.handle.clear();
	}
}

template<>
void Rendering::HAL::GLRenderbuffer::Bind() const
{
	glBindRenderbuffer(GL_RENDERBUFFER, GetID());
}

template<>
void Rendering::HAL::GLRenderbuffer::Unbind() const
{
	glBindRenderbuffer(GL_RENDERBUFFER, 0);
}

template<>
uint32_t Rendering::HAL::GLRenderbuffer::GetID() const
{
	return ::Rendering::Backend::Records::RenderbufferName(m_context.handle);
}

template<>
uint32_t Rendering::HAL::GLRenderbuffer::GetHandleId() const
{
	return m_context.handle.GetId();
}

template<>
void Rendering::HAL::GLRenderbuffer::Allocate(uint16_t p_width, uint16_t p_height, Settings::EInternalFormat p_format)
{
	m_context.width = p_width;
	m_context.height = p_height;
	m_context.format = p_format;

	::Rendering::Context::Driver* const driver = ::Rendering::HAL::CurrentDriver();
	if (driver != nullptr)
	{
		// The storage is GL, so it is recorded (Filament's allocateRenderbufferStorage); the size
		// stays here, because GetSize and the framebuffer's own size report read it.
		::Rendering::Backend::AllocateRenderbufferRequest request;
		request.handleId = m_context.handle.GetId();
		request.width = p_width;
		request.height = p_height;
		request.format = p_format;
		request.multiSample = isMultisample;
		driver->EnqueueAllocateRenderbuffer(request);
		m_context.allocated = true;
		return;
	}

	if (isMultisample) {
		glNamedRenderbufferStorageMultisample(GetID(),4, EnumToValue<GLenum>(m_context.format), m_context.width, m_context.height);
	}
	else {
		glNamedRenderbufferStorage(GetID(), EnumToValue<GLenum>(m_context.format), m_context.width, m_context.height);
	}
	

	m_context.allocated = true;
}

template<>
bool Rendering::HAL::GLRenderbuffer::IsValid() const
{
	return m_context.allocated;
}

template<>
void Rendering::HAL::GLRenderbuffer::Resize(uint16_t p_width, uint16_t p_height)
{
	
	Allocate(p_width, p_height, m_context.format);
}


template<>
uint16_t Rendering::HAL::GLRenderbuffer::GetWidth() const
{
	
	return m_context.width;
}

template<>
uint16_t Rendering::HAL::GLRenderbuffer::GetHeight() const
{

	return m_context.height;
}
