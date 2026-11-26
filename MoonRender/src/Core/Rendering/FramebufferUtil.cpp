#include<format>
#include <cstdio>
#include <functional>
#include <thread>
#include <Core/Rendering/FramebufferUtil.h>
#include <Rendering/HAL/Common/HALDriverAccess.h>
#include <Rendering/Context/Driver.h>
#include <Rendering/Data/Framebuffer.h>
#include <Rendering/Data/Renderbuffer.h>
#include <Rendering/Data/Texture.h>
#include <glad/glad.h>

namespace Core::Rendering::FramebufferUtil
{
	void SetupFramebuffer(
		::Rendering::Data::Framebuffer& p_framebuffer,
		const ::Rendering::Settings::TextureDesc& p_textureDesc,
		bool p_useDepth,
		bool p_useStencil,
		bool useMulSample
	)
	{
		using namespace ::Rendering::HAL;
		using namespace ::Rendering::Settings;

		if (std::getenv("MOON_FRAME_TRACE") != nullptr)
		{
			// Which thread is attaching, and is a context current there at all? An FBO name only
			// means something in the context that made it, so a mismatch shows up as tex=0 and a 0
			// status in the [fbo] attach trace.
			std::fprintf(stderr, "[fbo] setup name=%s id=%u ctx=%s driver=%s thread=%llu\n",
				p_framebuffer.GetDebugName().c_str(), p_framebuffer.GetID(),
				glGetString(GL_VERSION) != nullptr ? "current" : "NONE",
				::Rendering::HAL::CurrentDriver() != nullptr ? "yes" : "no",
				static_cast<unsigned long long>(std::hash<std::thread::id>{}(std::this_thread::get_id())));
			std::fflush(stderr);
		}

		const auto renderTexture = std::make_shared<::Rendering::Data::Texture>(
			useMulSample? ETextureType::TEXTURE_2DMULSAMPLE:ETextureType::TEXTURE_2D,
			std::format(
				"{}/Color",
				p_framebuffer.GetDebugName()
			)
		);

		renderTexture->Allocate(p_textureDesc);
		p_framebuffer.Attach(renderTexture, EFramebufferAttachment::COLOR);

		if (p_useDepth || p_useStencil)
		{
			const auto renderbuffer = std::make_shared<::Rendering::Data::Renderbuffer>(useMulSample);
			const auto internalFormat = p_useStencil
				? (useMulSample ? EInternalFormat::DEPTH32F_STENCIL8 : EInternalFormat::DEPTH_STENCIL)
				: EInternalFormat::DEPTH_COMPONENT;
			renderbuffer->Allocate(p_textureDesc.width, p_textureDesc.height, internalFormat);
			if (p_useStencil)
			{
				p_framebuffer.Attach(renderbuffer, EFramebufferAttachment::DEPTH_STENCIL);
			}
			else if (p_useDepth)
			{
				p_framebuffer.Attach(renderbuffer, EFramebufferAttachment::DEPTH);
			}
		}

		p_framebuffer.Validate();
	}

	void SetupFramebuffer(
		::Rendering::Data::Framebuffer& p_framebuffer,
		uint32_t p_width,
		uint32_t p_height,
		bool p_useDepth,
		bool p_useStencil,
		bool p_useMipMaps,
		bool useMulSample
	)
	{
		using namespace ::Rendering::HAL;
		using namespace ::Rendering::Settings;

		p_width = static_cast<uint16_t>(std::max(1u, p_width));
		p_height = static_cast<uint16_t>(std::max(1u, p_height));

		TextureDesc renderTextureDesc{
			.width = p_width,
			.height = p_height,
			.minFilter = p_useMipMaps ? ETextureFilteringMode::LINEAR_MIPMAP_LINEAR : ETextureFilteringMode::LINEAR,
			.magFilter = ETextureFilteringMode::LINEAR,
			.horizontalWrap = ETextureWrapMode::CLAMP_TO_BORDER,
			.verticalWrap = ETextureWrapMode::CLAMP_TO_BORDER,
			.internalFormat = EInternalFormat::RGBA32F,
			.useMipMaps = p_useMipMaps,
			.mutableDesc = MutableTextureDesc{
				.format = EFormat::RGBA,
				.type = EPixelDataType::FLOAT
			}
			
		};

		SetupFramebuffer(
			p_framebuffer,
			renderTextureDesc,
			p_useDepth,
			p_useStencil,
			useMulSample
		);
	}

	void SetupFramebufferWithDriver(
		::Rendering::Data::Framebuffer& p_framebuffer,
		const ::Rendering::Settings::TextureDesc& p_textureDesc,
		bool p_useDepth,
		bool p_useStencil,
		bool p_useMultiSample)
	{
		// The target is built on *this* side, one recorded command per step: the colour texture and
		// the renderbuffer exist here as front-end objects (handles), the attach commands name those
		// handles, and the executor only creates the GL names. That is Filament's split (the front
		// end owns the HwTexture handles, the driver makes the objects) and it is what keeps the
		// target's attachment list readable here - a pass that samples its own target asks for it at
		// record time, long before the executor has run anything.
		SetupFramebuffer(p_framebuffer, p_textureDesc, p_useDepth, p_useStencil, p_useMultiSample);
	}

	void SetupFramebufferWithDriver(
		::Rendering::Data::Framebuffer& p_framebuffer,
		uint32_t p_width,
		uint32_t p_height,
		bool p_useDepth,
		bool p_useStencil,
		bool p_useMipMaps,
		bool p_useMultiSample)
	{
		SetupFramebuffer(p_framebuffer, p_width, p_height, p_useDepth, p_useStencil, p_useMipMaps, p_useMultiSample);
	}
}