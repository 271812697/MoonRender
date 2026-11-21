#pragma once
#include <string>
#include <array>
#include <functional>
#include <thread>
#include <memory>
#include "Rendering/Backend/CreateTextureRequest.h"
#include "Rendering/Backend/UpdateTextureRequest.h"
#include <Rendering/Backend/TextureRequests.h>
#include <Rendering/Backend/OverlayRequests.h>
#include "Rendering/Backend/TextureUploadRequest.h"
#include "Rendering/Backend/BufferRequests.h"
#include "Rendering/Backend/DescriptorRequests.h"
#include "Rendering/Backend/VertexArrayRequests.h"
#include "Rendering/Backend/ProgramRequests.h"
#include "Rendering/Backend/ReadPixelsRequest.h"
#include "Rendering/Backend/FramebufferRequests.h"
#include "Rendering/Settings/DriverSettings.h"
#include "Rendering/Settings/ERenderingCapability.h"
#include "Rendering/Settings/EPrimitiveMode.h"
#include "Rendering/Settings/ERasterizationMode.h"
#include "Rendering/Settings/EComparaisonAlgorithm.h"
#include "Rendering/Settings/EOperation.h"
#include "Rendering/Settings/ECullFace.h"
#include "Rendering/Settings/ECullingOptions.h"
#include "Rendering/Settings/EPixelDataFormat.h"
#include "Rendering/Settings/EPixelDataType.h"
#include "Rendering/Data/PipelineState.h"
#include "Rendering/Resources/IMesh.h"
#include "Rendering/HAL/Framebuffer.h"
#include <Maths/FVector4.h>
#include <Tools/Utils/OptRef.h>
#include "Rendering/Context/IGLContextHost.h"
namespace Rendering::Backend
{
	class CommandBufferQueue;
	class CommandStream;
	class OpenGLDriver;
	struct DrawMaterialRequest;
	struct DrawMeshRequest;
	struct BindFramebufferRequest;
}
namespace Rendering::Context
{

	class Driver final
	{

	public:
		Driver(const Settings::DriverSettings& p_driverSettings);

		~Driver();

		/** Tells the driver which context the render module draws on. The window layer creates
		 * it (sharing objects with the window's own context) and hands it over; before that is
		 * set, GL calls go to whatever context the host left current - which is the coupling
		 * this removes. */
		void SetContextHost(IGLContextHost* p_contextHost);
		IGLContextHost* GetContextHost() const;

		/** Re-applies the global GL state the backend sets up (reversed-Z depth range, blend
		 * func, seamless cubemap, debug output). That state is per context, so a freshly made
		 * module context starts without it. Call with the module context current. */
		void ReapplyGlobalState();

		void OnFrameCompleted();
		void SetViewport(
			uint32_t p_x,
			uint32_t p_y,
			uint32_t p_width,
			uint32_t p_height
		);
		void Clear(
			bool p_colorBuffer,
			bool p_depthBuffer,
			bool p_stencilBuffer,
			const Maths::FVector4& p_color = Maths::FVector4::Zero,
			bool p_scissor = false
		);
		/** Sets the scissor rectangle; the test itself is enabled through the
		 * pipeline state (see PipelineState::scissorTest). */
		void SetScissor(
			uint32_t p_x,
			uint32_t p_y,
			uint32_t p_width,
			uint32_t p_height
		);
		void Draw(
			Rendering::Data::PipelineState p_pso,
			const Resources::IMesh& p_mesh,
			Settings::EPrimitiveMode p_primitiveMode = Settings::EPrimitiveMode::TRIANGLES,
			uint32_t p_instances = 1
		);
		void Draw(
			Rendering::Data::PipelineState p_pso,
			const Resources::IMesh& p_mesh,
			int index,
			Settings::EPrimitiveMode p_primitiveMode = Settings::EPrimitiveMode::TRIANGLES,
			uint32_t p_instances = 1
		);
		Data::PipelineState CreatePipelineState() const;
		std::string_view GetVendor() const;
		std::string_view GetHardware() const;
		std::string_view GetVersion() const;
		std::string_view GetShadingLanguageVersion() const;

	public:
		void SetPipelineState(Data::PipelineState p_state);
		void ResetPipelineState();
		/** The recording side of the command API (see DriverAPI.inc): each one writes a command
		 * into the stream and returns; nothing reaches GL until the batch is executed. */
#define DECL_DRIVER_COMMAND(name, paramsDecl, params) void Enqueue##name paramsDecl;
#include <Rendering/Backend/DriverAPI.inc>
#undef DECL_DRIVER_COMMAND
		/** Records p_command to be run when the batch is executed, on the render side.
		 *
		 * The escape hatch for GL that is not described by a command yet - an overlay renderer, a
		 * pass that still does its own GL. It is the migration tool (Filament calls it
		 * queueCommand) and it costs a heap allocation, so it is the exception, not the rule: what
		 * it wraps should become a command of its own (or move into the HAL/executor) as soon as
		 * it is hot. Everything the lambda touches must live until the batch runs, which is the
		 * same frame it was recorded in. */
		void Enqueue(std::function<void()> p_command);
		/** Uploads p_request.pixels into the texture p_request.handle names.
		 *
		 * A named driver method that records the matching command (EnqueueUploadTexture, see
		 * DriverAPI.inc): the descriptor and a copy of the bytes travel with the command, and the
		 * executor on the context side allocates and uploads them. Filament's
		 * createTexture/update2DImage pair has the same shape.
		 */
		void UploadTexture(Rendering::Backend::TextureUploadRequest p_request);
		/** Creates a texture on the side that owns the context (Filament's createTexture): the
		 * request names it by handle and describes it; the object exists once the command runs,
		 * which is before anything recorded after it. */
		void CreateTexture(Rendering::Backend::CreateTextureRequest p_request);
		/** Updates a sub-image of a texture that already has storage. */
		void UpdateTexture(Rendering::Backend::UpdateTextureRequest p_request);
		/** Binds p_target as the render target (null: the default framebuffer).
		 *
		 * Recorded as a command and executed immediately after, like SetPipelineState: a pass
		 * binds its target and then keeps issuing immediate GL (texture and uniform work that is
		 * not commandized yet), so the bind has to land before that starts. The drain goes away
		 * with the rest of them, when a frame becomes one batch. */
		void BindFramebuffer(const HAL::Framebuffer* p_target);

		/** Reads pixels on the side that owns the context (Filament's readPixels). */
		void ReadPixels(const ::Rendering::Backend::ReadPixelsRequest& p_request);

		/** Applies a vertex layout on the side that owns the context (glVertexAttribPointer needs it). */
		void ApplyVertexArrayLayout(const ::Rendering::Backend::SetVertexArrayLayoutRequest& p_request);

		/** Builds a program on the side that owns the context (Filament's createProgram): the
		 * stages' sources and the binding layout travel with the command, and the executor
		 * compiles, links and declares the descriptor slots there. */
		void CreateProgram(::Rendering::Backend::CreateProgramRequest p_request);

		/** Binds a program on the side that owns the context (Filament's bindProgram): 0 unbinds. */
		void BindProgram(uint32_t p_programId);
		/** Binds by handle: what a material uses, because the GL name of a program it only has a
		 * front-end handle for is not known on this side (docs/RenderThreadPlan.md §8.64). */
		void BindProgramHandle(uint32_t p_programHandleId);

		/** Declares a program's descriptor slots to GL, on the side that owns the context. */
		void DeclareProgramBindings(::Rendering::Backend::DeclareProgramBindingsRequest p_request);

		/** Declares a loose sampler's texture unit on the side that owns the context. */
		void SetSamplerUnit(uint32_t p_programId, std::string p_samplerName, uint32_t p_unit);

		/** Writes bytes into a GL buffer on the side that owns the context. The data is copied into
		 * the command, so it does not have to outlive this call (Filament's updateBufferObject). */
		void UpdateBufferRange(uint32_t p_bufferId, uint32_t p_offset, const void* p_data, uint32_t p_size);

		/** Binds one descriptor - a buffer range or a texture - as a command (see
		 * Backend::BindDescriptorRequest). */
		void BindDescriptor(const ::Rendering::Backend::BindDescriptorRequest& p_request);

		/** Builds a render target's attachments on the side that owns the context. */
		void SetupFramebuffer(HAL::Framebuffer& p_target, uint32_t p_width, uint32_t p_height,
			bool p_useDepth, bool p_useStencil, bool p_useMipMaps, bool p_useMultiSample);

		/** The descriptor form: the attachment's format comes from p_desc. */
		void SetupFramebuffer(HAL::Framebuffer& p_target, const Settings::TextureDesc& p_desc,
			bool p_useDepth, bool p_useStencil, bool p_useMultiSample);

		/** Resizes a render target on the side that owns the context (the attachments are GL). */
		void ResizeFramebuffer(HAL::Framebuffer& p_target, uint16_t p_width, uint16_t p_height);

		/** Blits one colour attachment of p_source into one of p_target (MSAA resolve, present). */
		void CopyFramebufferColor(HAL::Framebuffer& p_source, uint32_t p_sourceIndex,
			HAL::Framebuffer& p_target, uint32_t p_targetIndex);
		/** Depth blit between two targets (Filament's blit with TargetBufferFlags::DEPTH): the
		 * depth-peeling pass copies the opaque depth into its first layer with it. */
		void CopyFramebufferDepth(HAL::Framebuffer& p_source, HAL::Framebuffer& p_target);
		/** Front end: publishes what has been recorded as one batch (Filament's FEngine::flush ->
		 * flushCommandBuffer). Any thread may call it; only the recording thread may record, so
		 * in practice it is the frame's thread. Does not execute anything. */
		void Flush();
		/** Whether the batches are executed by a render thread of the engine's own. When they are,
		 * ExecutePendingCommands stops executing and becomes flush + wait (Filament's
		 * flushAndWait): the recording side must not run GL, and what it needs next - the frame's
		 * texture - is produced by the batch it just published. */
		void SetRenderThreadActive(bool p_active);
		/** True when the batches run on the engine's render thread (see SetRenderThreadActive). */
		bool IsRenderThreadActive() const { return m_renderThreadActive; }

		/** The thread the batches run on (invalid until SetRenderThreadActive(true)). */
		std::thread::id GetRenderThreadId() const { return m_renderThreadId; }
		/** Whether the calling thread may issue GL for this module: the thread the batches run on,
		 * or - without a render thread - whichever thread is calling (Filament's
		 * DriverBase::isDriverThread, which the HAL uses to decide between doing an operation and
		 * recording the command that does it). */
		bool IsDriverThread() const;
		/** Asks the command queue to stop handing out work: the render thread's loop then ends. */
		/** Makes the module context current on the thread that runs the batches, and re-applies the
		 * per-context GL state. Filament does this from FRenderer::beginFrame through
		 * OpenGLDriver::makeCurrent: the context is not made current once at thread start, it is made
		 * current for the frame that is about to be drawn. */
		/** Records the frame's first command: the executor makes the module's context current for
		 * the batch it is about to run (Filament's FRenderer::beginFrame -> FSwapChain::makeCurrent). */
		void MakeCurrent();

		void RequestExit();
		/** Back end: runs the batches that are waiting, on the calling thread, and gives the ring
		 * space back. This is what the render thread's loop calls (Filament's FEngine::execute:
		 * waitForCommands -> driver.execute -> releaseBuffer); executing it from the recording
		 * thread is what the single-threaded build does until the frame moves. */
		void ExecuteAll();
		/** Back end, blocking: waits for the next published batch, runs it, gives the ring back.
		 * False means the queue was asked to exit (Filament's FEngine::execute, which returns
		 * false to break the render thread's loop). */
		bool WaitAndExecute();
		/** Filament's Engine::execute: publishes the frame's recording and then runs it.
		 *
		 * With no render thread this call *is* the driver loop (FEngine::execute on the recording
		 * thread): it walks what was just published. With a render thread the batch belongs to that
		 * thread, so all this side can do is publish it and wait until it has run - the caller is
		 * the consumer (it pastes the frame), and reading a texture a batch is still writing would
		 * tear the frame.
		 *
		 * The frame is recorded by AView::Render, which only *flushes* (publishes) it; this is
		 * called once per frame, outside the render call, the way FilamentApp calls
		 * mEngine->execute() from its loop. */
		void Execute();
		/** Flush + wait: everything recorded so far has run when this returns. For the paths that
		 * read a result back immediately - a program's reflection after Link, a framebuffer's
		 * completeness, a pick readback (Filament's flushAndWait, minus the GPU fence). */
		void ExecutePendingCommands();
		/** The queue the commands are recorded into, for the executor side (and for the debug
		 * overlay that reports how much of the ring a frame used). */
		Backend::CommandBufferQueue& GetCommandBufferQueue();
		/** The writer side of the same ring. Internal: the Enqueue* methods above are the API;
		 * this is what the driver's own recording helper writes through. */
		Backend::CommandStream& GetCommandStream();

	private:
		IGLContextHost* m_contextHost = nullptr;
		std::string m_vendor;
		std::string m_hardware;
		std::string m_version;
		std::string m_shadingLanguageVersion;
		Data::PipelineState m_defaultPipelineState;
		/** The state the backend came up in (what Init returned). It seeds the executor's state
		 * machine (see ReapplyGlobalState); the applied state itself lives there, not here. */
		Data::PipelineState m_pipelineState;
		/** The command path: the ring the frame is recorded into, the writer, and the executor
		 * that runs the batches (see ExecutePendingCommands). */
		std::unique_ptr<Backend::CommandBufferQueue> m_commandQueue;
		std::unique_ptr<Backend::CommandStream> m_commandStream;
		std::unique_ptr<Backend::OpenGLDriver> m_openGLDriver;
		/** True while a batch is being executed: a driver call made from inside it runs right
		 * there instead of being recorded (see ExecutePendingCommands and the recording stubs). */
		bool m_executing = false;
		bool m_renderThreadActive = false;
		/** The thread the batches run on. ExecutePendingCommands has to tell it apart from the
		 * recording side: inside a command it must run inline, or it would wait for itself
		 * (Filament's isThisThreadTheDriverThread). */
		std::thread::id m_renderThreadId{};
	};
}
