#pragma once
#include <cstdint>
#include <unordered_map>

#include <Rendering/HAL/Backend.h>
#include <Rendering/Backend/CreateTextureRequest.h>
#include <Rendering/Backend/UpdateTextureRequest.h>
#include <Rendering/Backend/TextureRequests.h>
#include <Rendering/Backend/OverlayRequests.h>
#include <Rendering/Backend/TextureUploadRequest.h>
#include <Rendering/Backend/BufferRequests.h>
#include <Rendering/Backend/DescriptorRequests.h>
#include <Rendering/Backend/ProgramRequests.h>
#include <Rendering/Backend/ReadPixelsRequest.h>
#include <Rendering/Backend/VertexArrayRequests.h>
#include <Rendering/Backend/FramebufferRequests.h>
#include <Rendering/Context/IGLContextHost.h>
#include <Rendering/Backend/DrawMeshRequest.h>
#include <Rendering/Backend/BindFramebufferRequest.h>
#include <Rendering/Data/PipelineState.h>
#include <Rendering/Settings/ERenderingCapability.h>
#include <Rendering/Settings/EBlendingFactor.h>
#include <Rendering/Settings/EBlendingEquation.h>

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
		void BindFramebuffer(const ::Rendering::Backend::BindFramebufferRequest& p_request);
		void SetClearColor(float p_red, float p_green, float p_blue, float p_alpha);
		void Clear(bool p_colorBuffer, bool p_depthBuffer, bool p_stencilBuffer);
		void CreateTexture(::Rendering::Backend::CreateTextureRequest p_request);
		void UpdateTexture(::Rendering::Backend::UpdateTextureRequest p_request);
		void UploadTexture(::Rendering::Backend::TextureUploadRequest p_request);
		/** glTextureParameterfv(GL_TEXTURE_BORDER_COLOR): a sampler parameter on a texture the
		 * caller only holds a handle for. */
		void SetTextureBorderColor(const ::Rendering::Backend::SetTextureBorderColorRequest& p_request);
		void GenerateTextureMipmaps(const ::Rendering::Backend::GenerateTextureMipmapsRequest& p_request);
		/** glNamedFramebufferTexture{,Layer}: attaches a texture the target's owner already has. */
		void AttachTexture(const ::Rendering::Backend::AttachTextureRequest& p_request);
		/** glNamedFramebufferDrawBuffers: which colour attachments the target writes. */
		void SetFramebufferDrawBuffers(const ::Rendering::Backend::SetFramebufferDrawBuffersRequest& p_request);
		/** glNamedFramebufferRenderbuffer: attaches a renderbuffer the target's owner already has. */
		void AttachRenderbuffer(const ::Rendering::Backend::AttachRenderbufferRequest& p_request);
		/** glNamedRenderbufferStorage{,Multisample}. */
		void AllocateRenderbuffer(const ::Rendering::Backend::AllocateRenderbufferRequest& p_request);
		/** Depth blit between two targets. */
		void CopyFramebufferDepth(const ::Rendering::Backend::CopyFramebufferDepthRequest& p_request);
		void AllocateBuffer(::Rendering::Backend::AllocateBufferRequest p_request);
		/** Binds a buffer to a target or an indexed binding point, on the side that owns the
		 * context: the light SSBO binding used to be issued from the recording side, which left it
		 * unbound on the executor (docs §8.67). */
		void BindBuffer(const ::Rendering::Backend::BindBufferRequest& p_request);
		void CreateBuffer(::Rendering::Backend::CreateBufferRequest p_request);
		void CreateVertexArray(uint32_t p_handleId);
		void CreateFramebuffer(uint32_t p_handleId);
		/** Makes the module's context current on the executor's thread and re-applies the global GL
		 * state. Filament's OpenGLDriver::makeCurrent (called by FRenderer::beginFrame through the
		 * swap chain): the context is made current for the frame that is about to be drawn. */
		void MakeCurrent();
		/** Blocks until the GPU has finished everything issued so far: a fence on the command stream
		 * plus a wait for it.
		 *
		 * The consumer side needs this. It waits for a batch before presenting it (Driver::Execute ->
		 * WaitForIdle), and a batch is only presentable once it is *drawn* - not when its commands
		 * have been issued. Filament's swap chain does the same with its frame fences: the consumer
		 * is handed a finished frame, one frame at a time, never a skipped or half-drawn one. Called
		 * on the side that owns the context, at the end of a batch. */
		void WaitForFrameCompletion();
		void SetContextHost(::Rendering::Context::IGLContextHost* p_contextHost) { m_contextHost = p_contextHost; }

		void CreateProgram(::Rendering::Backend::CreateProgramRequest p_request);
		/** glUseProgram on the side that owns the context; 0 unbinds (Filament's bindProgram). */
		void BindProgram(uint32_t p_programId);
		/** The same bind, named by the program handle: resolves the GL name on this side, after the
		 * create command has run. */
		void BindProgramHandle(uint32_t p_programHandleId);
		void DeclareProgramBindings(::Rendering::Backend::DeclareProgramBindingsRequest p_request);
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
		void ReadPixels(const ::Rendering::Backend::ReadPixelsRequest& p_request);
		void SetVertexArrayLayout(const ::Rendering::Backend::SetVertexArrayLayoutRequest& p_request);
		void UpdateBuffer(const ::Rendering::Backend::UpdateBufferRequest& p_request);
		void BindDescriptor(const ::Rendering::Backend::BindDescriptorRequest& p_request);
		void SetupFramebuffer(const ::Rendering::Backend::SetupFramebufferRequest& p_request);
		void ResizeFramebuffer(const ::Rendering::Backend::ResizeFramebufferRequest& p_request);
		void CopyFramebufferColor(const ::Rendering::Backend::CopyFramebufferColorRequest& p_request);
		void CreateRenderbuffer(uint32_t p_handleId);

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
