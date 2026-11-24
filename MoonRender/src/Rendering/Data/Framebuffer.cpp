#include <Rendering/Data/Framebuffer.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

#include <Rendering/Backend/BackendResources.h>
#include <Rendering/Backend/FramebufferRequests.h>
#include <Rendering/Backend/HandleAllocator.h>
#include <Rendering/Context/Driver.h>
#include <Rendering/HAL/Common/HALDriverAccess.h>

namespace Rendering::Data
{
	namespace
	{
		/** How many bytes one pixel of p_format/p_type takes: what the readback's CPU buffer has to
		 * hold. Only the formats the engine reads back are spelled out; everything else assumes the
		 * widest (4 channels of 4 bytes), which over-allocates rather than truncates. */
		uint32_t PixelByteSize(Settings::EPixelDataFormat p_format, Settings::EPixelDataType p_type)
		{
			uint32_t channels = 4;
			switch (p_format)
			{
			case Settings::EPixelDataFormat::RED:
			case Settings::EPixelDataFormat::GREEN:
			case Settings::EPixelDataFormat::BLUE:
			case Settings::EPixelDataFormat::ALPHA:
			case Settings::EPixelDataFormat::COLOR_INDEX:
			case Settings::EPixelDataFormat::STENCIL_INDEX:
			case Settings::EPixelDataFormat::DEPTH_COMPONENT:
			case Settings::EPixelDataFormat::LUMINANCE:
				channels = 1;
				break;
			case Settings::EPixelDataFormat::LUMINANCE_ALPHA:
				channels = 2;
				break;
			case Settings::EPixelDataFormat::RGB:
			case Settings::EPixelDataFormat::BGR:
				channels = 3;
				break;
			default:
				channels = 4;
				break;
			}

			uint32_t typeSize = 4;
			switch (p_type)
			{
			case Settings::EPixelDataType::BYTE:
			case Settings::EPixelDataType::UNSIGNED_BYTE:
				typeSize = 1;
				break;
			case Settings::EPixelDataType::SHORT:
			case Settings::EPixelDataType::UNSIGNED_SHORT:
				typeSize = 2;
				break;
			default:
				typeSize = 4;
				break;
			}

			return channels * typeSize;
		}

		/** The backend object this target's handle names: the framebuffer itself, on the side that
		 * owns the context. Null until its create command has run. */
		Rendering::HAL::Framebuffer* Resolve(
			const Rendering::Backend::ResourceHandle<Rendering::Backend::HwFramebuffer>& p_handle)
		{
			return Rendering::Backend::GetHandleAllocator()
				.HandleCast<Rendering::HAL::Framebuffer>(p_handle);
		}
	}

	Framebuffer::Framebuffer(std::string_view p_debugName) : m_debugName(p_debugName)
	{
		// createRenderTargetS/createRenderTargetR: the handle is taken here and the object (with its
		// GL name) is made where the context is.
		m_handle = Rendering::Backend::GetHandleAllocator()
			.Allocate<Rendering::Backend::HwFramebuffer>();

		if (::Rendering::Context::Driver* const driver = ::Rendering::HAL::CurrentDriver())
		{
			driver->EnqueueCreateFramebuffer(m_handle.GetId());
		}
		else
		{
			Rendering::Backend::Records::CreateFramebuffer(m_handle);
		}
	}

	Framebuffer::~Framebuffer()
	{
		Rendering::Backend::Records::DestroyFramebuffer(m_handle);
	}

	Framebuffer::Framebuffer(Framebuffer&& p_other) noexcept
		: m_handle(p_other.m_handle)
		, m_valid(p_other.m_valid)
		, m_debugName(std::move(p_other.m_debugName))
		, m_attachments(std::move(p_other.m_attachments))
	{
		p_other.m_handle.clear();
		p_other.m_attachments.clear();
	}

	Framebuffer& Framebuffer::operator=(Framebuffer&& p_other) noexcept
	{
		if (this != &p_other)
		{
			Rendering::Backend::Records::DestroyFramebuffer(m_handle);

			m_handle = p_other.m_handle;
			m_valid = p_other.m_valid;
			m_debugName = std::move(p_other.m_debugName);
			m_attachments = std::move(p_other.m_attachments);

			p_other.m_handle.clear();
			p_other.m_attachments.clear();
		}
		return *this;
	}

	void Framebuffer::Bind() const
	{
		if (Rendering::HAL::Framebuffer* const framebuffer = Resolve(m_handle))
		{
			framebuffer->Bind();
		}
	}

	void Framebuffer::Unbind() const
	{
		if (Rendering::HAL::Framebuffer* const framebuffer = Resolve(m_handle))
		{
			framebuffer->Unbind();
		}
	}

	bool Framebuffer::Validate()
	{
		// With a driver the target's attachments are attached on the executor, so the status here
		// would describe a state this thread cannot see. The bind trace ([fbo] bind ... status=) is
		// where it is checked instead - in both configurations, because the recording code is the
		// same (docs/RenderThreadPlan.md §8.72).
		if (::Rendering::HAL::CurrentDriver() != nullptr)
		{
			return true;
		}

		const Rendering::HAL::Framebuffer* const framebuffer = Resolve(m_handle);
		return m_valid = framebuffer != nullptr && framebuffer->CheckStatus();
	}

	void Framebuffer::Resize(uint16_t p_width, uint16_t p_height)
	{
		// A resize is each attachment re-specifying its storage: recorded for a texture, recorded for
		// a renderbuffer (see Data::Renderbuffer), so this is pure front-end bookkeeping.
		for (auto& attachment : m_attachments)
		{
			if (const auto texture = std::get_if<std::shared_ptr<Rendering::HAL::Texture>>(&attachment.second);
				texture != nullptr && *texture)
			{
				(*texture)->Resize(p_width, p_height);
			}
			else if (const auto renderbuffer = std::get_if<std::shared_ptr<Rendering::Data::Renderbuffer>>(&attachment.second);
				renderbuffer != nullptr && *renderbuffer)
			{
				(*renderbuffer)->Resize(p_width, p_height);
			}
		}
	}

	void Framebuffer::AttachTexture(
		std::shared_ptr<Rendering::HAL::Texture> p_texture,
		Settings::EFramebufferAttachment p_attachment,
		uint32_t p_index,
		std::optional<uint32_t> p_layer,
		uint32_t p_level)
	{
		if (p_texture == nullptr)
		{
			return;
		}

		const uint32_t key = AttachmentKey(p_attachment, p_index);
		::Rendering::Context::Driver* const driver = ::Rendering::HAL::CurrentDriver();

		if (driver != nullptr && p_texture->GetHandleId() != 0 && !p_texture->HasBackendName())
		{
			// The texture is a front-end object and the target is a handle: attaching is GL, so it is
			// recorded with the two handles and done where the context is (Filament's createRenderTarget
			// naming its attachments instead of making them).
			Rendering::Backend::AttachTextureRequest request;
			request.framebufferHandleId = m_handle.GetId();
			request.textureHandleId = p_texture->GetHandleId();
			request.attachment = p_attachment;
			request.index = p_index;
			request.level = p_level;
			request.useLayer = p_layer.has_value();
			request.layer = p_layer.value_or(0);
			driver->EnqueueAttachTexture(request);

			m_attachments[key] = std::move(p_texture);
			return;
		}

		// No driver (or a texture that already carries its own name): the attach is applied here,
		// through the target's backend object.
		if (Rendering::HAL::Framebuffer* const framebuffer = Resolve(m_handle))
		{
			if (p_layer.has_value())
			{
				framebuffer->AttachTextureLayer(p_attachment, p_index, p_texture->GetID(), p_level,
					p_layer.value());
			}
			else
			{
				framebuffer->AttachTexture(p_attachment, p_index, p_texture->GetID(), p_level);

				if (std::getenv("MOON_FRAME_TRACE") != nullptr)
				{
					std::fprintf(stderr, "[fbo] attach fbo=%u att=0x%x tex=%u status=0x%04x\n",
						framebuffer->GetID(), static_cast<unsigned>(p_attachment), p_texture->GetID(),
						framebuffer->GetStatus());
					std::fflush(stderr);
				}
			}
		}

		m_attachments[key] = std::move(p_texture);
	}

	void Framebuffer::AttachRenderbuffer(
		std::shared_ptr<Rendering::Data::Renderbuffer> p_renderbuffer,
		Settings::EFramebufferAttachment p_attachment,
		uint32_t p_index)
	{
		if (p_renderbuffer == nullptr)
		{
			return;
		}

		const uint32_t key = AttachmentKey(p_attachment, p_index);

		if (::Rendering::Context::Driver* const driver = ::Rendering::HAL::CurrentDriver())
		{
			// Same as the texture attachment: the target and the renderbuffer are named here, the
			// attach happens where the context is.
			Rendering::Backend::AttachRenderbufferRequest request;
			request.framebufferHandleId = m_handle.GetId();
			request.renderbufferHandleId = p_renderbuffer->GetHandle().GetId();
			request.attachment = p_attachment;
			request.index = p_index;
			driver->EnqueueAttachRenderbuffer(request);

			m_attachments[key] = std::move(p_renderbuffer);
			return;
		}

		if (Rendering::HAL::Framebuffer* const framebuffer = Resolve(m_handle))
		{
			p_renderbuffer->Bind();
			framebuffer->AttachRenderbuffer(p_attachment, p_index, p_renderbuffer->GetID());
		}

		m_attachments[key] = std::move(p_renderbuffer);
	}

	void Framebuffer::SetDrawBuffers(const std::vector<uint32_t>& p_indices)
	{
		if (::Rendering::Context::Driver* const driver = ::Rendering::HAL::CurrentDriver())
		{
			Rendering::Backend::SetFramebufferDrawBuffersRequest request;
			request.framebufferHandleId = m_handle.GetId();
			request.indices = p_indices;
			driver->EnqueueSetFramebufferDrawBuffers(request);
			return;
		}

		if (Rendering::HAL::Framebuffer* const framebuffer = Resolve(m_handle))
		{
			framebuffer->SetDrawBuffers(p_indices);
		}
	}

	void Framebuffer::SetTargetDrawBuffer(std::optional<uint32_t> p_index)
	{
		if (::Rendering::Context::Driver* const driver = ::Rendering::HAL::CurrentDriver())
		{
			// One draw buffer is the single-entry form of SetDrawBuffers; the sky capture switches
			// the cube face it writes this way.
			Rendering::Backend::SetFramebufferDrawBuffersRequest request;
			request.framebufferHandleId = m_handle.GetId();
			if (p_index.has_value())
			{
				request.indices.push_back(p_index.value());
			}
			driver->EnqueueSetFramebufferDrawBuffers(request);
			return;
		}

		if (Rendering::HAL::Framebuffer* const framebuffer = Resolve(m_handle))
		{
			framebuffer->SetDrawBuffer(p_index);
		}
	}

	void Framebuffer::SetTargetReadBuffer(std::optional<uint32_t> p_index)
	{
		if (::Rendering::Context::Driver* const driver = ::Rendering::HAL::CurrentDriver())
		{
			// Recorded: picking selects one attachment, reads it, then selects another, and the two
			// commands run in that order where the context is.
			Rendering::Backend::SetFramebufferReadBufferRequest request;
			request.framebufferHandleId = m_handle.GetId();
			request.hasIndex = p_index.has_value();
			request.index = p_index.value_or(0);
			driver->EnqueueSetFramebufferReadBuffer(request);
			return;
		}

		if (Rendering::HAL::Framebuffer* const framebuffer = Resolve(m_handle))
		{
			framebuffer->SetReadBuffer(p_index);
		}
	}

	uint32_t Framebuffer::GetID() const
	{
		return Rendering::Backend::Records::FramebufferName(m_handle);
	}

	std::pair<uint16_t, uint16_t> Framebuffer::GetSize(
		Settings::EFramebufferAttachment p_attachment) const
	{
		for (const auto& attachment : m_attachments)
		{
			if (const auto texture = std::get_if<std::shared_ptr<Rendering::HAL::Texture>>(&attachment.second);
				texture != nullptr && *texture)
			{
				return { (*texture)->GetDesc().width, (*texture)->GetDesc().height };
			}
			if (const auto renderbuffer = std::get_if<std::shared_ptr<Rendering::Data::Renderbuffer>>(&attachment.second);
				renderbuffer != nullptr && *renderbuffer)
			{
				return { (*renderbuffer)->GetWidth(), (*renderbuffer)->GetHeight() };
			}
		}
		return { {}, {} };
	}

	void Framebuffer::BlitToBackBuffer(uint16_t p_backBufferWidth, uint16_t p_backBufferHeight) const
	{
		const auto [width, height] = GetSize(Settings::EFramebufferAttachment::COLOR);
		if (Rendering::HAL::Framebuffer* const framebuffer = Resolve(m_handle))
		{
			framebuffer->BlitTo(0, width, height, p_backBufferWidth, p_backBufferHeight);
		}
	}

	void Framebuffer::ReadPixels(
		uint32_t p_x,
		uint32_t p_y,
		uint32_t p_width,
		uint32_t p_height,
		Settings::EPixelDataFormat p_format,
		Settings::EPixelDataType p_type,
		void* p_data) const
	{
		if (p_data == nullptr || p_width == 0 || p_height == 0)
		{
			return;
		}

		const size_t byteCount = static_cast<size_t>(p_width) * p_height
			* PixelByteSize(p_format, p_type);

		::Rendering::Context::Driver* const driver = ::Rendering::HAL::CurrentDriver();
		if (driver == nullptr)
		{
			if (Rendering::HAL::Framebuffer* const framebuffer = Resolve(m_handle))
			{
				framebuffer->ReadPixels(p_x, p_y, p_width, p_height, p_format, p_type, p_data);
			}
			return;
		}

		// A recorded readback, then a wait for it: the caller needs the bytes right here (picking, the
		// screenshot path), so this is one of the synchronous driver calls (FlushAndWait). The read
		// buffer is whatever a recorded SetFramebufferReadBuffer selected.
		auto bytes = std::make_shared<std::vector<uint8_t>>(byteCount);

		Rendering::Backend::ReadPixelsRequest request;
		request.framebufferHandleId = m_handle.GetId();
		request.readBufferIndex = -1;
		request.x = p_x;
		request.y = p_y;
		request.width = p_width;
		request.height = p_height;
		request.format = p_format;
		request.type = p_type;
		request.data = bytes;

		driver->EnqueueReadPixels(request);
		driver->FlushAndWait();

		std::memcpy(p_data, bytes->data(), std::min(byteCount, bytes->size()));
	}

	void Framebuffer::ReadPixelsToBuffer(
		const Rendering::Backend::ResourceHandle<Rendering::Backend::HwBufferObject>& p_buffer,
		uint64_t p_offset,
		uint32_t p_x,
		uint32_t p_y,
		uint32_t p_width,
		uint32_t p_height,
		Settings::EPixelDataFormat p_format,
		Settings::EPixelDataType p_type) const
	{
		::Rendering::Context::Driver* const driver = ::Rendering::HAL::CurrentDriver();
		if (driver == nullptr)
		{
			const uint32_t buffer = Rendering::Backend::Records::BufferName(p_buffer);
			if (buffer == 0)
			{
				return;
			}
			if (Rendering::HAL::Framebuffer* const framebuffer = Resolve(m_handle))
			{
				framebuffer->ReadPixelsToBuffer(buffer, p_offset, p_x, p_y, p_width, p_height, p_format,
					p_type);
			}
			return;
		}

		// Asynchronous: the GPU writes the pixel-pack buffer, and the bytes are fetched later
		// (DownloadBuffer) once the buffer's fence is signalled - the frame never waits for it.
		Rendering::Backend::ReadPixelsToBufferRequest request;
		request.framebufferHandleId = m_handle.GetId();
		request.bufferHandleId = p_buffer.GetId();
		request.offset = p_offset;
		request.x = p_x;
		request.y = p_y;
		request.width = p_width;
		request.height = p_height;
		request.format = p_format;
		request.type = p_type;
		driver->EnqueueReadPixelsToBuffer(std::move(request));
	}

	void Framebuffer::Clear(Settings::EFramebufferAttachment p_attachment, int p_index)
	{
		if (Rendering::HAL::Framebuffer* const framebuffer = Resolve(m_handle))
		{
			framebuffer->Clear(p_attachment, p_index);
		}
	}
}
