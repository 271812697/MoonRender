#pragma once
#include <cstdint>
#include <optional>
#include <vector>

#include <Rendering/Backend/BackendResources.h>
#include <Rendering/HAL/Common/BackendConstructionTag.h>
#include <Rendering/Settings/EFramebufferAttachment.h>
#include <Rendering/Settings/EGraphicsBackend.h>
#include <Rendering/Settings/EPixelDataFormat.h>
#include <Rendering/Settings/EPixelDataType.h>

namespace Rendering::HAL
{
	/**
	* Represents a render target: the framebuffer object a pass draws into.
	*
	* This is the *backend* object, like TBuffer and TVertexArray: it is constructed in the slot a
	* handle names (see Backend::Records::CreateFramebuffer), it owns the GL name, and its methods
	* are the GL operations the executor runs (attach, choose the colour attachments to write,
	* check the status, blit, read back). The recording side holds a handle, wrapped by
	* Rendering::Data::Framebuffer, which also keeps the attachment list a pass reads back.
	*/
	template<Settings::EGraphicsBackend Backend, class Context>
	class TFramebuffer : public Rendering::Backend::HwFramebuffer
	{
	public:
		/**
		* Creates the backend object: the GL name is made here, so this runs where the context is.
		*/
		TFramebuffer(BackendConstructionTag);

		/** Does not touch GL: the name is deleted by Backend::Records::DestroyFramebuffer. */
		~TFramebuffer() = default;

		void Bind() const;
		void Unbind() const;

		/** glNamedFramebufferTexture: attaches a texture that already exists. */
		void AttachTexture(
			Settings::EFramebufferAttachment p_attachment,
			uint32_t p_index,
			uint32_t p_textureId,
			uint32_t p_level) const;

		/** glNamedFramebufferTextureLayer: one face of a cube map, one layer of an array. */
		void AttachTextureLayer(
			Settings::EFramebufferAttachment p_attachment,
			uint32_t p_index,
			uint32_t p_textureId,
			uint32_t p_level,
			uint32_t p_layer) const;

		/** glNamedFramebufferRenderbuffer: attaches a renderbuffer that already exists. */
		void AttachRenderbuffer(
			Settings::EFramebufferAttachment p_attachment,
			uint32_t p_index,
			uint32_t p_renderbufferId) const;

		/** glNamedFramebufferDrawBuffers: which colour attachments the target writes. An empty list
		 * is the single-buffer form that writes nothing. */
		void SetDrawBuffers(const std::vector<uint32_t>& p_indices) const;

		/** glNamedFramebufferDrawBuffer / glNamedFramebufferReadBuffer; nullopt selects none. */
		void SetDrawBuffer(std::optional<uint32_t> p_index) const;
		void SetReadBuffer(std::optional<uint32_t> p_index) const;

		/** glCheckNamedFramebufferStatus == GL_FRAMEBUFFER_COMPLETE. */
		bool CheckStatus() const;

		/** The raw glCheckNamedFramebufferStatus result, for the diagnostic trace. */
		uint32_t GetStatus() const;

		/** glBlitNamedFramebuffer: copies the colour attachment into another framebuffer (the
		 * default one, for a present blit). */
		void BlitTo(
			uint32_t p_targetId,
			uint16_t p_srcWidth,
			uint16_t p_srcHeight,
			uint16_t p_dstWidth,
			uint16_t p_dstHeight) const;

		/** glClearNamedFramebuffer{fv,iv}: clears one attachment of this target. */
		void Clear(Settings::EFramebufferAttachment p_attachment, int p_index) const;

		/** Synchronous readback from the currently selected read buffer into CPU memory. */
		void ReadPixels(
			uint32_t p_x,
			uint32_t p_y,
			uint32_t p_width,
			uint32_t p_height,
			Settings::EPixelDataFormat p_format,
			Settings::EPixelDataType p_type,
			void* p_data) const;

		/** The same, into a pixel-pack buffer that already exists. */
		void ReadPixelsToBuffer(
			uint32_t p_pixelPackBufferId,
			uint64_t p_offset,
			uint32_t p_x,
			uint32_t p_y,
			uint32_t p_width,
			uint32_t p_height,
			Settings::EPixelDataFormat p_format,
			Settings::EPixelDataType p_type) const;

		/** Returns the GL name of the framebuffer. */
		uint32_t GetID() const;

	protected:
		Context m_context;
	};
}
