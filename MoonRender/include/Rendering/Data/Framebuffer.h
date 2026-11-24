#pragma once
#include <concepts>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include <Rendering/Backend/ResourceHandle.h>
#include <Rendering/Data/Renderbuffer.h>
#include <Rendering/HAL/Framebuffer.h>
#include <Rendering/HAL/Texture.h>
#include <Rendering/Settings/EFramebufferAttachment.h>
#include <Rendering/Settings/EPixelDataFormat.h>
#include <Rendering/Settings/EPixelDataType.h>
#include <Tools/Utils/OptRef.h>

namespace Rendering::Backend
{
	struct HwBufferObject;
	struct HwFramebuffer;
}

namespace Rendering::Data
{
	/** The front end of a render target: what a pass owns and binds.
	 *
	 * It holds the handle that names the backend object - HAL::Framebuffer, which lives in the
	 * handle's slot on the side that owns the context and is the only thing that has a GL name -
	 * plus the attachment list the recording side reads back (a pass that samples its own target
	 * asks for the attachment at record time, long before the executor has run anything), the debug
	 * name and the validity the GL status reports. The counterpart of Data::BufferObject.
	 */
	class Framebuffer
	{
	public:
		/** What can be attached: a texture (the colour/depth attachment a material may also sample)
		 * or a renderbuffer (the depth/stencil storage). Both are front-end objects here. */
		using Attachment = std::variant<
			std::shared_ptr<Rendering::HAL::Texture>,
			std::shared_ptr<Rendering::Data::Renderbuffer>
		>;

		template<typename T>
		static constexpr bool IsSupportedAttachmentType =
			std::same_as<T, Rendering::HAL::Texture> ||
			std::same_as<T, Rendering::Data::Renderbuffer>;

		/** Reserves the handle and records the creation of its backend object. */
		explicit Framebuffer(std::string_view p_debugName = std::string_view{});

		/** Frees the record now and queues the GL deletion to the side that owns the context. */
		~Framebuffer();

		Framebuffer(const Framebuffer&) = delete;
		Framebuffer& operator=(const Framebuffer&) = delete;
		Framebuffer(Framebuffer&& p_other) noexcept;
		Framebuffer& operator=(Framebuffer&& p_other) noexcept;

		/** Binds the target. Synchronous: a bind is GL, so it only means something where the
		 * context is (the render path records Driver::BindFramebuffer instead). */
		void Bind() const;
		void Unbind() const;

		/** Checks the target's status. With a driver this side cannot see the attachments (they are
		 * attached on the executor), so it reports true; the [fbo] bind trace is where the status is
		 * checked then. */
		bool Validate();

		bool IsValid() const { return m_valid; }

		/** Resizes every attachment this target holds (each one re-specifies its storage). */
		void Resize(uint16_t p_width, uint16_t p_height);

		/** Attaches p_toAttach at p_attachment (+p_index), recording the attach when that has to
		 * happen on the side that owns the context. */
		template<class T>
			requires IsSupportedAttachmentType<T>
		void Attach(
			std::shared_ptr<T> p_toAttach,
			Settings::EFramebufferAttachment p_attachment,
			uint32_t p_index = 0,
			std::optional<uint32_t> p_layer = std::nullopt)
		{
			if constexpr (std::same_as<T, Rendering::HAL::Texture>)
			{
				AttachTexture(std::move(p_toAttach), p_attachment, p_index, p_layer, 0);
			}
			else
			{
				AttachRenderbuffer(std::move(p_toAttach), p_attachment, p_index);
			}
		}

		/** The same, but one mip level of a layered texture (the sky capture's prefilter). */
		template<class T>
			requires IsSupportedAttachmentType<T>
		void AttachLevel(
			std::shared_ptr<T> p_toAttach,
			Settings::EFramebufferAttachment p_attachment,
			uint32_t p_index = 0,
			uint32_t p_level = 0,
			std::optional<uint32_t> p_layer = std::nullopt)
		{
			if constexpr (std::same_as<T, Rendering::HAL::Texture>)
			{
				AttachTexture(std::move(p_toAttach), p_attachment, p_index, p_layer, p_level);
			}
			else
			{
				AttachRenderbuffer(std::move(p_toAttach), p_attachment, p_index);
			}
		}

		/** The attachment at p_attachment (+p_index), or empty when there is none. */
		template<class T>
			requires IsSupportedAttachmentType<T>
		Tools::Utils::OptRef<T> GetAttachment(
			Settings::EFramebufferAttachment p_attachment,
			uint32_t p_index = 0)
		{
			if (const auto it = m_attachments.find(AttachmentKey(p_attachment, p_index));
				it != m_attachments.end())
			{
				if (const auto pval = std::get_if<std::shared_ptr<T>>(&it->second);
					pval != nullptr && *pval)
				{
					return **pval;
				}
			}
			return std::nullopt;
		}

		/** The same attachment, kept alive: a pass that also samples its target's colour or depth
		 * owns the texture through this. Empty when the target has no such attachment (yet). */
		template<class T>
			requires IsSupportedAttachmentType<T>
		std::shared_ptr<T> GetAttachmentShared(
			Settings::EFramebufferAttachment p_attachment,
			uint32_t p_index = 0) const
		{
			if (const auto it = m_attachments.find(AttachmentKey(p_attachment, p_index));
				it != m_attachments.end())
			{
				if (const auto pval = std::get_if<std::shared_ptr<T>>(&it->second); pval != nullptr)
				{
					return *pval;
				}
			}
			return nullptr;
		}

		/** glNamedFramebufferDrawBuffers, recorded when there is a driver to record into. */
		void SetDrawBuffers(const std::vector<uint32_t>& p_indices);
		void SetTargetDrawBuffer(std::optional<uint32_t> p_index);

		/** Selects the attachment a readback reads. Synchronous: it is one half of the immediate
		 * readback pair (see ReadPixels). */
		void SetTargetReadBuffer(std::optional<uint32_t> p_index);

		/** The GL name, resolved on this side; 0 until the create command has run. */
		uint32_t GetID() const;

		std::pair<uint16_t, uint16_t> GetSize(
			Settings::EFramebufferAttachment p_attachment = Settings::EFramebufferAttachment::COLOR
		) const;

		/** Copies the colour attachment into the default framebuffer (a full-screen blit). */
		void BlitToBackBuffer(uint16_t p_backBufferWidth, uint16_t p_backBufferHeight) const;

		/** Synchronous readback into CPU memory. TODO: it and SetTargetReadBuffer have to become a
		 * command pair, like the rest of the render path (docs/RenderThreadPlan.md §8.72). */
		void ReadPixels(
			uint32_t p_x,
			uint32_t p_y,
			uint32_t p_width,
			uint32_t p_height,
			Settings::EPixelDataFormat p_format,
			Settings::EPixelDataType p_type,
			void* p_data) const;

		/** Asynchronous readback into a pixel-pack buffer: glReadPixels with the buffer bound; the
		 * bytes must be fetched later (MapRead) once the buffer's fence is signalled. */
		void ReadPixelsToBuffer(
			const Rendering::Backend::ResourceHandle<Rendering::Backend::HwBufferObject>& p_buffer,
			uint64_t p_offset,
			uint32_t p_x,
			uint32_t p_y,
			uint32_t p_width,
			uint32_t p_height,
			Settings::EPixelDataFormat p_format,
			Settings::EPixelDataType p_type) const;

		/** Clears one attachment of this target. */
		void Clear(Settings::EFramebufferAttachment p_attachment, int p_index);

		const std::string& GetDebugName() const { return m_debugName; }

		const Rendering::Backend::ResourceHandle<Rendering::Backend::HwFramebuffer>& GetHandle() const
		{
			return m_handle;
		}

	private:
		static uint32_t AttachmentKey(
			Settings::EFramebufferAttachment p_attachment,
			uint32_t p_index)
		{
			return (static_cast<uint32_t>(p_attachment) << 8) | (p_index & 0xFFu);
		}

		void AttachTexture(
			std::shared_ptr<Rendering::HAL::Texture> p_texture,
			Settings::EFramebufferAttachment p_attachment,
			uint32_t p_index,
			std::optional<uint32_t> p_layer,
			uint32_t p_level);

		void AttachRenderbuffer(
			std::shared_ptr<Rendering::Data::Renderbuffer> p_renderbuffer,
			Settings::EFramebufferAttachment p_attachment,
			uint32_t p_index);

		Rendering::Backend::ResourceHandle<Rendering::Backend::HwFramebuffer> m_handle;
		bool m_valid = false;
		std::string m_debugName;
		std::unordered_map<uint32_t, Attachment> m_attachments;
	};
}
