#include <Rendering/Data/Framebuffer.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

#include <Rendering/Backend/BackendResources.h>
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

	}

	Framebuffer::Framebuffer(std::string_view p_debugName) : m_debugName(p_debugName)
	{
		// Filament's createRenderTarget: the driver reserves the handle and records the create that
		// makes the object (and its GL name) where the context is.
		m_handle = Rendering::Context::Driver::Get().CreateFramebufferObject(m_debugName);
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
		// Recorded like every other operation on a handle: the bind happens where the context is.
		Rendering::Context::Driver::Get().BindFramebufferObject(m_handle);
	}

	void Framebuffer::Unbind() const
	{
		// A null handle binds the default framebuffer.
		Rendering::Context::Driver::Get().BindFramebufferObject({});
	}

	bool Framebuffer::Validate()
	{
		// The target's attachments are attached on the side that owns the context, so the status here
		// would describe a state this thread cannot see. The bind trace ([fbo] bind ... status=) is
		// where it is checked instead - the executor prints it with the GL status when
		// MOON_FRAME_TRACE is set (docs/RenderThreadPlan.md §8.72).
		return m_valid = true;
	}

	void Framebuffer::Resize(uint16_t p_width, uint16_t p_height)
	{
		// A resize is each attachment re-specifying its storage: recorded for a texture, recorded for
		// a renderbuffer (see Data::Renderbuffer), so this is pure front-end bookkeeping.
		for (auto& attachment : m_attachments)
		{
			if (const auto texture = std::get_if<std::shared_ptr<Rendering::Data::Texture>>(&attachment.second);
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
		std::shared_ptr<Rendering::Data::Texture> p_texture,
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

		// Filament's createRenderTarget taking TargetBufferInfo: the texture is named by its handle
		// and the executor does glNamedFramebufferTexture, so neither side issues GL by hand.
		Rendering::Context::Driver::Get().AttachTextureObject(
			m_handle, p_texture->GetHandle(), p_attachment, p_index, p_level,
			p_layer.has_value(), p_layer.value_or(0));

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

		// Same as the texture attachment: the target and the renderbuffer travel as handles.
		Rendering::Context::Driver::Get().AttachRenderbufferObject(
			m_handle, p_renderbuffer->GetHandle(), p_attachment, p_index);

		m_attachments[key] = std::move(p_renderbuffer);
	}

	/** glNamedFramebufferDrawBuffers: which colour attachments the target writes (the gbuffer writes
	 * two). Recorded, so the selection lands before the draws that depend on it. */
	void Framebuffer::SetDrawBuffers(const std::vector<uint32_t>& p_indices)
	{
		Rendering::Context::Driver::Get().SetFramebufferDrawBuffersObject(m_handle, p_indices);
	}

	void Framebuffer::SetTargetDrawBuffer(std::optional<uint32_t> p_index)
	{
		// One draw buffer is the single-entry form of SetDrawBuffers; the sky capture switches the
		// cube face it writes this way.
		std::vector<uint32_t> indices;
		if (p_index.has_value())
		{
			indices.push_back(p_index.value());
		}
		Rendering::Context::Driver::Get().SetFramebufferDrawBuffersObject(m_handle, std::move(indices));
	}

	void Framebuffer::SetTargetReadBuffer(std::optional<uint32_t> p_index)
	{
		// Recorded: picking selects one attachment, reads it, then selects another, and the two
		// commands run in that order where the context is.
		Rendering::Context::Driver::Get().SetFramebufferReadBufferObject(
			m_handle, p_index.has_value(), p_index.value_or(0));
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
			if (const auto texture = std::get_if<std::shared_ptr<Rendering::Data::Texture>>(&attachment.second);
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
		// glBlitNamedFramebuffer into the default framebuffer, as one command: this side knows both
		// sizes (its attachment list, and the caller's back buffer).
		const auto [width, height] = GetSize(Settings::EFramebufferAttachment::COLOR);
		Rendering::Context::Driver::Get().BlitFramebufferObject(
			m_handle, width, height, p_backBufferWidth, p_backBufferHeight);
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

		// A recorded readback, then a wait for it: the caller needs the bytes right here (picking, the
		// screenshot path), so this is one of the synchronous driver calls (FlushAndWait). The
		// attachment is whatever a recorded SetTargetReadBuffer selected.
		auto bytes = std::make_shared<std::vector<uint8_t>>(byteCount);
		Rendering::Context::Driver& driver = Rendering::Context::Driver::Get();
		driver.ReadPixelsObject(m_handle, bytes, p_x, p_y, p_width, p_height, p_format, p_type, {});
		driver.FlushAndWait();

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
		// Asynchronous: the GPU writes the pixel-pack buffer, and the bytes are fetched later
		// (DownloadBuffer) once the buffer's fence is signalled - the frame never waits for it.
		Rendering::Context::Driver::Get().ReadPixelsToBufferObject(
			m_handle, p_buffer, p_offset, p_x, p_y, p_width, p_height, p_format, p_type);
	}

	void Framebuffer::Clear(Settings::EFramebufferAttachment p_attachment, int p_index)
	{
		// glClearNamedFramebuffer* on the target the handle names, where the context is.
		Rendering::Context::Driver::Get().ClearFramebufferAttachmentObject(
			m_handle, p_attachment, p_index);
	}
}
