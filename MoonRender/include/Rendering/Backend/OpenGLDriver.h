#pragma once
#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>

#include <Rendering/HAL/Backend.h>
#include <Rendering/Backend/OverlayRequests.h>
#include <Rendering/Backend/BufferDescriptor.h>
#include <Rendering/Backend/PixelBufferDescriptor.h>
#include <Rendering/Settings/EFramebufferAttachment.h>
#include <Rendering/Backend/BufferRequests.h>
#include <Rendering/Settings/EAccessSpecifier.h>
#include <Rendering/Backend/DescriptorRequests.h>
#include <Rendering/Backend/ProgramRequests.h>
#include <Rendering/Backend/VertexArrayRequests.h>
#include <Rendering/Context/IGLContextHost.h>
#include <Rendering/Backend/DrawMeshRequest.h>
#include <Rendering/Data/PipelineState.h>
#include <Rendering/Settings/ERenderingCapability.h>
#include <Rendering/Settings/EBlendingFactor.h>
#include <Rendering/Settings/EBlendingEquation.h>

namespace Editor
{
	namespace Panels {
		class AView;
	}
}
namespace Rendering::Backend
{
	/** The executor side of the driver: it runs the commands the main thread recorded, and it is
	 * the only place a GL call for the command path is allowed to happen.
	 *
	 * Today it is called from the main thread (Driver::ExecutePendingCommands) right where the GL
	 * calls used to be, so the recorded order and the drawing order stay identical. When the
	 * frame moves to its own thread, this class moves with it: it owns nothing that has to be
	 * destroyed on the GUI thread, and everything it touches (the HAL backend, the GL context) is
	 * already created on the render side.
	 *
	 * The executors themselves are hand-declared below: this class is the bottom of the stack and
	 * Driver, so the two halves cannot drift apart. */
	class OpenGLDriver
	{
	public:
		explicit OpenGLDriver(::Rendering::HAL::Backend& p_backend) noexcept : m_backend(p_backend) {}

		OpenGLDriver(const OpenGLDriver&) = delete;
		OpenGLDriver& operator=(const OpenGLDriver&) = delete;

		/** Walks one batch - the commands recorded in [p_begin, p_end) - and runs them in order.
		 * Stops at the first command whose next is nullptr (the terminating NoopCommand). */
		void Execute(void* p_begin, void* p_end);

		/** Makes the executor believe p_state is what the backend has, without touching GL.
		 *
		 * The driver applies a global state when it initialises the backend (reversed-Z depth
		 * range, blend func, ...), and the pipeline state it returns is what the backend ends up
		 * in; the diff that follows has to start from there, not from a default-constructed
		 * struct, or the first frame would skip every setting it thinks is already applied. */
		void ResetAppliedState(const ::Rendering::Data::PipelineState& p_state);

		// The executors: these are what a command ends up calling. Hand-written on purpose - this
		// class must not know that commands or a command stream exist (see CommandStream).
		void Viewport(int32_t p_x, int32_t p_y, int32_t p_width, int32_t p_height);
		void Scissor(int32_t p_x, int32_t p_y, int32_t p_width, int32_t p_height);
		void Capability(::Rendering::Settings::ERenderingCapability p_capability, bool p_enabled);
		void BlendingEquation(::Rendering::Settings::EBlendingEquation p_equation);
		void BlendingFunction(::Rendering::Settings::EBlendingFactor p_source, ::Rendering::Settings::EBlendingFactor p_destination);
		void PipelineState(::Rendering::Data::PipelineState p_state);
		void DrawMesh(const ::Rendering::Backend::DrawMeshRequest& p_request);
		void SetClearColor(float p_red, float p_green, float p_blue, float p_alpha);
		void Clear(bool p_colorBuffer, bool p_depthBuffer, bool p_stencilBuffer);
		/** glTextureParameterfv(GL_TEXTURE_BORDER_COLOR): a sampler parameter on a texture the
		 * caller only holds a handle for. */
		void CopyFramebufferColorObject(
			::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwFramebuffer> p_source,
			uint32_t p_sourceIndex,
			uint16_t p_sourceWidth, uint16_t p_sourceHeight,
			::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwFramebuffer> p_target,
			uint32_t p_targetIndex,
			uint16_t p_targetWidth, uint16_t p_targetHeight);
		void CopyFramebufferDepthObject(
			::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwFramebuffer> p_source,
			uint16_t p_sourceWidth, uint16_t p_sourceHeight,
			::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwFramebuffer> p_target,
			uint16_t p_targetWidth, uint16_t p_targetHeight);
		void BlitFramebufferObject(
			::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwFramebuffer> p_source,
			uint16_t p_sourceWidth, uint16_t p_sourceHeight,
			uint16_t p_targetWidth, uint16_t p_targetHeight);
		void ClearFramebufferAttachmentObject(
			::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwFramebuffer> p_framebuffer,
			::Rendering::Settings::EFramebufferAttachment p_attachment, int32_t p_index);
		void ReadPixelsObject(
			::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwFramebuffer> p_framebuffer,
			std::shared_ptr<std::vector<uint8_t>> p_data,
			uint32_t p_x, uint32_t p_y, uint32_t p_width, uint32_t p_height,
			::Rendering::Settings::EPixelDataFormat p_format,
			::Rendering::Settings::EPixelDataType p_type,
			std::function<void()> p_onDone);
		void ReadPixelsToBufferObject(
			::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwFramebuffer> p_framebuffer,
			::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwBufferObject> p_buffer,
			uint64_t p_offset,
			uint32_t p_x, uint32_t p_y, uint32_t p_width, uint32_t p_height,
			::Rendering::Settings::EPixelDataFormat p_format,
			::Rendering::Settings::EPixelDataType p_type);

		// Render targets (Filament's createRenderTarget / bindFramebuffer / setAttachments): the
		// recording side names the target and its attachments as handles, this side does the GL.
		void CreateFramebufferObject(
			::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwFramebuffer> p_framebuffer,
			std::string p_debugName);
		void BindFramebufferObject(
			::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwFramebuffer> p_framebuffer);
		void AttachTextureObject(
			::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwFramebuffer> p_framebuffer,
			::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwTexture> p_texture,
			::Rendering::Settings::EFramebufferAttachment p_attachment,
			uint32_t p_index, uint32_t p_level, bool p_hasLayer, uint32_t p_layer);
		void AttachRenderbufferObject(
			::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwFramebuffer> p_framebuffer,
			::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwRenderbuffer> p_renderbuffer,
			::Rendering::Settings::EFramebufferAttachment p_attachment, uint32_t p_index);
		void SetFramebufferDrawBuffersObject(
			::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwFramebuffer> p_framebuffer,
			std::vector<uint32_t> p_indices);
		void SetFramebufferReadBufferObject(
			::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwFramebuffer> p_framebuffer,
			bool p_hasIndex, uint32_t p_index);

		// Vertex arrays, renderbuffers and programs: the create hands back the handle, the rest is
		// one command per operation, resolved on this side.
		void CreateVertexArrayObject(
			::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwVertexArray> p_array,
			std::string p_debugName);
		void SetVertexArrayObjectLayout(
			::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwVertexArray> p_array,
			::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwBufferObject> p_vertexBuffer,
			::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwBufferObject> p_indexBuffer,
			std::shared_ptr<const std::vector<::Rendering::Backend::VertexLayoutAttribute>> p_attributes);
		void CreateRenderbufferObject(
			::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwRenderbuffer> p_renderbuffer,
			std::string p_debugName);
		void AllocateRenderbufferObject(
			::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwRenderbuffer> p_renderbuffer,
			uint16_t p_width, uint16_t p_height,
			::Rendering::Settings::EInternalFormat p_format,
			bool p_multiSample);
		void CreateProgramObject(
			::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwProgram> p_program,
			std::shared_ptr<const std::vector<::Rendering::Backend::ShaderSourceSpec>> p_stages,
			std::shared_ptr<const ::Rendering::Data::DescriptorSetInfo> p_bindings);
		void BindProgramObject(
			::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwProgram> p_program);
		void DeclareProgramBindingsObject(
			::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwProgram> p_program,
			std::shared_ptr<const ::Rendering::Data::DescriptorSetInfo> p_bindings);

		// The texture half of the driver API: the create makes the object in the slot the recording
		// side reserved, and each later call resolves the handle and does the GL.
		void CreateTextureObject(
			::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwTexture> p_texture,
			::Rendering::Settings::ETextureType p_type,
			std::string p_debugName);
		void AllocateTextureObject(
			::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwTexture> p_texture,
			::Rendering::Settings::TextureDesc p_desc,
			::Rendering::Backend::PixelBufferDescriptor p_pixels);
		void UpdateTextureObject(
			::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwTexture> p_texture,
			uint32_t p_x, uint32_t p_y, uint32_t p_width, uint32_t p_height,
			::Rendering::Backend::PixelBufferDescriptor p_pixels);
		void UploadTextureObject(
			::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwTexture> p_texture,
			::Rendering::Settings::TextureDesc p_desc,
			::Rendering::Backend::PixelBufferDescriptor p_pixels,
			bool p_generateMipmaps);
		void GenerateTextureMipmapsObject(
			::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwTexture> p_texture);
		void SetTextureBorderColorObject(
			::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwTexture> p_texture,
			::Maths::FVector4 p_color);

		// The buffer half of the driver API (see DriverAPI.inc): each one is the command the
		// recording side queued, and each one resolves the handle to the backend object that lives
		// in its slot. Filament's createBufferObjectR / allocateBufferObject / updateBufferObject /
		// bindBuffer / unmapBuffer / insertFence.
		void CreateBufferObject(
			::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwBufferObject> p_buffer,
			::Rendering::Settings::EBufferType p_type);
		void AllocateBufferObject(
			::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwBufferObject> p_buffer,
			uint64_t p_size,
			::Rendering::Settings::EAccessSpecifier p_usage);
		void UpdateBufferObject(
			::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwBufferObject> p_buffer,
			::Rendering::Backend::BufferDescriptor p_data,
			uint64_t p_offset);
		/** glBindBuffer / glBindBufferBase: the target, or one of the indexed binding points (the
		 * light SSBO used to be bound from the recording side, which left it unbound on the
		 * executor - docs §8.67). */
		void BindBufferObject(
			::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwBufferObject> p_buffer,
			::Rendering::Settings::EBufferType p_type,
			std::optional<uint32_t> p_index);
		/** Binds nothing to p_type's target: the inverse of a plain BindBufferObject. */
		void UnbindBufferObject(::Rendering::Settings::EBufferType p_type);
		/** glBindBufferRange: a byte range at an indexed binding point (a block of the pool). */
		void BindBufferObjectRange(
			::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwBufferObject> p_buffer,
			uint32_t p_index,
			uint64_t p_offset,
			uint64_t p_size);
		/** glUnmapNamedBuffer on the buffer's backend object. */
		void UnmapBufferObject(
			::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwBufferObject> p_buffer);
		/** glFenceSync on the buffer's backend object (the GPU progress signal a readback waits
		 * for). */
		void InsertBufferFence(
			::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwBufferObject> p_buffer);
		/** glDeleteSync on the buffer's backend object. */
		void ClearBufferFence(
			::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwBufferObject> p_buffer);
		/** Makes the module's context current on the executor's thread and re-applies the global GL
		 * state. Filament's OpenGLDriver::makeCurrent (called by FRenderer::beginFrame through the
		 * swap chain): the context is made current for the frame that is about to be drawn. */
		void MakeCurrent();
		void SetContextHost(::Rendering::Context::IGLContextHost* p_contextHost) { m_contextHost = p_contextHost; }

		/** Uploads the overlay's per-frame vertex stream into its buffer and describes the vertex
		 * format on its array (attribute pointers too: the stride varies per draw). */
		void UploadOverlayVertices(::Rendering::Backend::UploadOverlayVerticesRequest p_request);
		/** Binds a vertex array and draws from it (the overlay's own draw call). */
		void DrawVertexArray(const ::Rendering::Backend::DrawVertexArrayRequest& p_request);
		/** Diagnostic: reads a texture back and prints its first bytes (texture buffers too). */
		void PrintTextureBytes(uint32_t p_textureHandleId, uint32_t p_byteCount, const char* p_label);
		/** One entry of the 2D overlay's list: array + scissor + texture + indexed range. */
		void DrawIndexedVertexArray(const ::Rendering::Backend::DrawIndexedVertexArrayRequest& p_request);
		/** A loose mat4 uniform on a program the overlay owns (its projection). */
		void SetProgramMatrix4(const ::Rendering::Backend::ProgramMatrix4Request& p_request);
		void DownloadBuffer(::Rendering::Backend::DownloadBufferRequest p_request);
		void BindDescriptor(const ::Rendering::Backend::BindDescriptorRequest& p_request);
		void CommitAView();

	private:
		::Rendering::HAL::Backend& m_backend;
		::Rendering::Context::IGLContextHost* m_contextHost = nullptr;
		/** What the backend is in right now: the state machine of the pipeline lives here, next
		 * to the GL calls it describes. */
		::Rendering::Data::PipelineState m_appliedState;
		/** Program -> draws of the frame being executed, printed at the next MakeCurrent under
		 * MOON_FRAME_TRACE: two modes that pick different shader variants for the same geometry show
		 * it here before anywhere else. */
		std::unordered_map<uint32_t, uint32_t> m_frameProgramDraws;
		uint32_t m_frameIndex = 0;
	};
}
