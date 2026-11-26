#include <Rendering/Data/Renderbuffer.h>

#include <Rendering/Backend/BackendResources.h>
#include <Rendering/Backend/HandleAllocator.h>
#include <Rendering/Context/Driver.h>
#include <Rendering/HAL/Common/HALDriverAccess.h>
#include <Rendering/HAL/Renderbuffer.h>

namespace Rendering::Data
{
	namespace
	{
		Rendering::HAL::Renderbuffer* Resolve(
			const Rendering::Backend::ResourceHandle<Rendering::Backend::HwRenderbuffer>& p_handle)
		{
			return Rendering::Backend::GetHandleAllocator()
				.HandleCast<Rendering::HAL::Renderbuffer>(p_handle);
		}
	}

	Renderbuffer::Renderbuffer(bool p_multiSample) : m_multiSample(p_multiSample)
	{
		// Filament's createRenderbuffer: the driver reserves the handle and records the create that
		// gives it its backend object where the context is.
		m_handle = Rendering::Context::Driver::Get().CreateRenderbufferObject("");
	}

	Renderbuffer::~Renderbuffer()
	{
		Rendering::Backend::Records::DestroyRenderbuffer(m_handle);
	}

	Renderbuffer::Renderbuffer(Renderbuffer&& p_other) noexcept
		: m_handle(p_other.m_handle)
		, m_width(p_other.m_width)
		, m_height(p_other.m_height)
		, m_format(p_other.m_format)
		, m_multiSample(p_other.m_multiSample)
		, m_allocated(p_other.m_allocated)
	{
		p_other.m_handle.clear();
	}

	Renderbuffer& Renderbuffer::operator=(Renderbuffer&& p_other) noexcept
	{
		if (this != &p_other)
		{
			Rendering::Backend::Records::DestroyRenderbuffer(m_handle);

			m_handle = p_other.m_handle;
			m_width = p_other.m_width;
			m_height = p_other.m_height;
			m_format = p_other.m_format;
			m_multiSample = p_other.m_multiSample;
			m_allocated = p_other.m_allocated;

			p_other.m_handle.clear();
		}
		return *this;
	}

	void Renderbuffer::Allocate(uint16_t p_width, uint16_t p_height, Settings::EInternalFormat p_format)
	{
		m_width = p_width;
		m_height = p_height;
		m_format = p_format;

		// The storage is GL, so it is recorded (Filament's allocateRenderbufferStorage); the size
		// stays here, because the framebuffer's own size report reads it.
		Rendering::Context::Driver::Get().AllocateRenderbufferObject(
			m_handle, p_width, p_height, p_format, m_multiSample);
		m_allocated = true;
	}

	void Renderbuffer::Resize(uint16_t p_width, uint16_t p_height)
	{
		Allocate(p_width, p_height, m_format);
	}

	void Renderbuffer::Bind() const
	{
		if (Rendering::HAL::Renderbuffer* const renderbuffer = Resolve(m_handle))
		{
			renderbuffer->Bind();
		}
	}

	uint32_t Renderbuffer::GetID() const
	{
		return Rendering::Backend::Records::RenderbufferName(m_handle);
	}
}
