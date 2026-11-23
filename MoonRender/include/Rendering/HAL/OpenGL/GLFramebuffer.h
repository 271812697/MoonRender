#pragma once
#include <unordered_map>
#include <Rendering/Backend/ResourceHandle.h>
#include <Rendering/HAL/Common/TFramebuffer.h>
#include <Rendering/HAL/OpenGL/GLTexture.h>
#include <Rendering/HAL/OpenGL/GLRenderbuffer.h>

namespace Rendering::Backend
{
	struct HwFramebuffer;
}

namespace Rendering::HAL
{
	template<Settings::EGraphicsBackend Backend, class GLTextureContext, class GLTextureHandleContext, class GLRenderbufferContext>
	struct TGLFramebufferContext
	{
		using Attachment = TFramebufferAttachment<Backend, GLTextureContext, GLTextureHandleContext, GLRenderbufferContext>;

		/** A handle; the name is in the record it names (Backend::HwFramebuffer), on the side that
		 * owns the context. */
		Rendering::Backend::ResourceHandle<Rendering::Backend::HwFramebuffer> handle;
		bool valid = false;
		std::string debugName;
		std::unordered_map<uint32_t, Attachment> attachments;
	};

	using GLFramebufferContext = TGLFramebufferContext<
		Settings::EGraphicsBackend::OPENGL,
		GLTextureContext,
		GLTextureHandleContext,
		GLRenderbufferContext
	>;

	using GLFramebuffer = TFramebuffer<
		Settings::EGraphicsBackend::OPENGL,
		GLFramebufferContext,
		GLTextureContext,
		GLTextureHandleContext,
		GLRenderbufferContext
	>;
}
