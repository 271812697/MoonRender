#pragma once
#include <variant>
#include <Rendering/Settings/DriverSettings.h>
#include <Rendering/Settings/ERenderingCapability.h>
#include <Rendering/Settings/EPrimitiveMode.h>
#include <Rendering/Settings/ERasterizationMode.h>
#include <Rendering/Settings/EComparaisonAlgorithm.h>
#include <Rendering/Settings/EOperation.h>
#include <Rendering/Settings/ECullFace.h>
#include <Rendering/Settings/ECullingOptions.h>
#include <Rendering/Settings/EPixelDataFormat.h>
#include <Rendering/Settings/EPixelDataType.h>
#include <Rendering/Settings/EGraphicsBackend.h>
#include <Rendering/Settings/EFramebufferAttachment.h>
#include <Rendering/Data/PipelineState.h>
#include <Rendering/Resources/Texture.h>
#include <Rendering/HAL/Common/TTexture.h>
#include <Rendering/HAL/Common/TRenderbuffer.h>
#include <Rendering/HAL/Buffer.h>
#include <Tools/Utils/OptRef.h>

namespace Rendering::HAL
{
	template<Settings::EGraphicsBackend Backend, class TextureContext, class TextureHandleContext, class RenderBufferContext>
	using TFramebufferAttachment = std::variant<
		std::shared_ptr<TTexture<Backend, TextureContext, TextureHandleContext>>,
		std::shared_ptr<TRenderbuffer<Backend, RenderBufferContext>>
	>;
	template<Settings::EGraphicsBackend Backend, class FramebufferContext, class TextureContext, class TextureHandleContext, class RenderBufferContext>
	class TFramebuffer
	{
	public:
		using Attachment = TFramebufferAttachment<Backend, TextureContext, TextureHandleContext, RenderBufferContext>;
		template<typename T>
		static constexpr bool IsSupportedAttachmentType =
			std::same_as<T, TTexture<Backend, TextureContext, TextureHandleContext>> ||
			std::same_as<T, TRenderbuffer<Backend, RenderBufferContext>>;
		TFramebuffer(std::string_view p_debugName = std::string_view{});
		~TFramebuffer();
		void Bind() const;
		void Unbind() const;
		bool Validate();
		bool IsValid() const;
		void Resize(uint16_t p_width, uint16_t p_height);
		template<class T>
			requires IsSupportedAttachmentType<T>
		void Attach(std::shared_ptr<T> p_toAttach,
			Settings::EFramebufferAttachment p_attachment,
			uint32_t p_index = 0,
			std::optional<uint32_t> p_layer = std::nullopt
		);
		template<class T>
			requires IsSupportedAttachmentType<T>
		void AttachLevel(std::shared_ptr<T> p_toAttach,
			Settings::EFramebufferAttachment p_attachment,
			uint32_t p_index = 0,
			uint32_t p_level=0,
			std::optional<uint32_t> p_layer = std::nullopt
		);
		template<class T>
			requires IsSupportedAttachmentType<T>
		Tools::Utils::OptRef<T> GetAttachment(
			Rendering::Settings::EFramebufferAttachment p_attachment,
			uint32_t p_index = 0
		) ;

		/** The same attachment, kept alive: a pass that also samples its target's colour or depth
		 * (the gbuffer, the mirror plane) owns the texture through this, and hands it to materials
		 * as a property. Empty when the target has no such attachment (yet). */
		template<class T>
			requires IsSupportedAttachmentType<T>
		std::shared_ptr<T> GetAttachmentShared(
			Rendering::Settings::EFramebufferAttachment p_attachment,
			uint32_t p_index = 0
		) const;
		void SetDrawBuffers(const std::vector<uint32_t>&p_index);
		void SetTargetDrawBuffer(std::optional<uint32_t> p_index);
		void SetTargetReadBuffer(std::optional<uint32_t> p_index);
		uint32_t GetID() const;
		std::pair<uint16_t, uint16_t> GetSize(
			Settings::EFramebufferAttachment p_attachment = Settings::EFramebufferAttachment::COLOR
		) const;
		void BlitToBackBuffer(uint16_t p_backBufferWidth, uint16_t p_backBufferHeight) const;
		void ReadPixels(
			uint32_t p_x,
			uint32_t p_y,
			uint32_t p_width,
			uint32_t p_height,
			Settings::EPixelDataFormat p_format,
			Settings::EPixelDataType p_type,
			void* p_data
		) const;

		/** Asynchronously copies pixels into a pixel pack buffer.
		 *
		 * glReadPixels writes into whatever is currently bound to
		 * GL_PIXEL_PACK_BUFFER; with a buffer bound, p_offset is a byte offset
		 * inside it. The call returns immediately, and the data must be fetched
		 * later (MapRead) once the buffer's fence is signalled, so the CPU never
		 * waits for the GPU.
		 */
		void ReadPixelsToBuffer(
			const ::Rendering::HAL::Buffer& p_buffer,
			uint64_t p_offset,
			uint32_t p_x,
			uint32_t p_y,
			uint32_t p_width,
			uint32_t p_height,
			Settings::EPixelDataFormat p_format,
			Settings::EPixelDataType p_type
		) const;
		const std::string& GetDebugName() const;

		void Clear(Settings::EFramebufferAttachment p_attachment,int index);
	protected:
		FramebufferContext m_context;
	};
}
