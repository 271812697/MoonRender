#pragma once
#include <cstdint>
#include <vector>

#include <Rendering/Data/Framebuffer.h>
#include <Rendering/Settings/TextureDesc.h>


namespace Rendering::Backend
{
	/** Asks the driver to build a render target's attachments on the side that owns the context.
	 *
	 * The split is Filament's: the recording side names the target and describes it, the executor
	 * creates the colour texture and the depth/stencil renderbuffer and attaches them
	 * (glNamedFramebufferTexture, glNamedFramebufferRenderbuffer, glDrawBuffers). A target whose
	 * attachments were made on the recording thread has a name but nothing behind it, which is
	 * exactly the "framebuffer is not complete" every FBO call then reports.
	 */
	struct SetupFramebufferRequest
	{
		::Rendering::Data::Framebuffer* target = nullptr;
		uint32_t width = 1;
		uint32_t height = 1;
		bool useDepth = false;
		bool useStencil = false;
		bool useMipMaps = false;
		bool useMultiSample = false;
		/** Set when the attachment's format comes from a descriptor rather than the default
		 * RGBA32F the width/height form uses (the present target is RGBA8, for instance). */
		bool useDesc = false;
		::Rendering::Settings::TextureDesc desc;
	};

	/** Asks the driver to resize a render target on the side that owns the context.
	 *
	 * A resize creates and frees attachments - texture storage, renderbuffers - so it is GL like
	 * everything else here. The target travels as the object itself, the same way
	 * BindFramebufferRequest carries its target: the frame owns the view's framebuffers and they
	 * outlive the batch.
	 */
	struct ResizeFramebufferRequest
	{
		::Rendering::Data::Framebuffer* target = nullptr;
		uint16_t width = 0;
		uint16_t height = 0;
	};

	/** Copies one colour attachment of p_source into one of p_target (a blit). Used for the MSAA
	 * resolve and for handing the frame from the scene target to the present target. */
	struct CopyFramebufferColorRequest
	{
		::Rendering::Data::Framebuffer* source = nullptr;
		uint32_t sourceIndex = 0;
		::Rendering::Data::Framebuffer* target = nullptr;
		uint32_t targetIndex = 0;
	};

	/** Attaches a texture that already exists to one of a target's attachment points (Filament's
	 * createRenderTarget taking TargetBufferInfo: the texture is named, not built here).
	 *
	 * This is what lets a pass own its attachments as HAL textures - a colour buffer the materials
	 * also sample, a depth texture the floor shader reads - without either side issuing GL: the
	 * front end names the two objects, the executor does glNamedFramebufferTexture.
	 */
	struct AttachTextureRequest
	{
		uint32_t framebufferHandleId = 0;
		uint32_t textureHandleId = 0;
		::Rendering::Settings::EFramebufferAttachment attachment =
			::Rendering::Settings::EFramebufferAttachment::COLOR;
		uint32_t index = 0;
		uint32_t level = 0;
		/** Set when the attachment is one layer of a layered texture (glNamedFramebufferTextureLayer). */
		bool useLayer = false;
		uint32_t layer = 0;
	};

	/** glNamedFramebufferReadBuffer: which attachment a readback reads. Picking selects one colour
	 * attachment, reads it, then selects another (the id, then the world position), so this is the
	 * first half of a readback pair that is recorded and executed in order. */
	struct SetFramebufferReadBufferRequest
	{
		uint32_t framebufferHandleId = 0;
		/** The colour attachment to select; false selects none (GL_NONE). */
		bool hasIndex = false;
		uint32_t index = 0;
	};

	/** glNamedFramebufferDrawBuffers: which colour attachments a target writes (the gbuffer pass
	 * writes two). */
	struct SetFramebufferDrawBuffersRequest
	{
		uint32_t framebufferHandleId = 0;
		std::vector<uint32_t> indices;
	};

	/** Attaches a renderbuffer that already exists to one of a target's attachment points. The
	 * depth buffer of the sky capture is the main user: its size changes per prefilter mip. */
	struct AttachRenderbufferRequest
	{
		uint32_t framebufferHandleId = 0;
		uint32_t renderbufferHandleId = 0;
		::Rendering::Settings::EFramebufferAttachment attachment =
			::Rendering::Settings::EFramebufferAttachment::DEPTH;
		uint32_t index = 0;
	};

	/** glNamedRenderbufferStorage{,Multisample}: the storage of a renderbuffer, which is GL like
	 * every other allocation. */
	struct AllocateRenderbufferRequest
	{
		uint32_t handleId = 0;
		uint16_t width = 1;
		uint16_t height = 1;
		::Rendering::Settings::EInternalFormat format = ::Rendering::Settings::EInternalFormat::RGBA;
		bool multiSample = false;
	};

	/** Copies the depth of p_source into p_target (a blit). Used by the layer path and by HZB's
	 * MSAA resolve. */
	struct CopyFramebufferDepthRequest
	{
		::Rendering::Data::Framebuffer* source = nullptr;
		::Rendering::Data::Framebuffer* target = nullptr;
	};
}
