#pragma once
#include <string>
#include <mutex>
#include <condition_variable>
#include <array>
#include <functional>
#include <optional>
#include <thread>
#include <memory>
#include <Rendering/Backend/OverlayRequests.h>
#include "Rendering/Backend/BufferRequests.h"
#include <Rendering/Backend/BufferDescriptor.h>
#include <Rendering/Backend/PixelBufferDescriptor.h>
#include "Rendering/Backend/BackendResources.h"
#include "Rendering/Backend/DescriptorRequests.h"
#include "Rendering/Backend/VertexArrayRequests.h"
#include "Rendering/Backend/ProgramRequests.h"
#include "Rendering/Settings/DriverSettings.h"
#include "Rendering/Settings/EAccessSpecifier.h"
#include "Rendering/Settings/EBufferType.h"
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
#include "Rendering/Data/Framebuffer.h"
#include <Maths/FVector4.h>
#include <Tools/Utils/OptRef.h>
#include "Rendering/Context/IGLContextHost.h"
namespace Rendering::Backend
{
	class CommandBufferQueue;
	class CommandStream;
	class OpenGLDriver;
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
		 * into the stream and returns; nothing reaches GL until the batch is executed.
		 *
		 * DriverAPI.inc declares three flavours and every consumer of it (this header, Driver.cpp,
		 * CommandStream, OpenGLDriver) expands all three:
		 *  - DECL_DRIVER_COMMAND: the recording method is Enqueue<name>, the historical entry point
		 *    of the commands that were here first;
		 *  - DECL_DRIVER_API / DECL_DRIVER_API_RETURN: Filament's driver API proper. The method
		 *    keeps its name on every side - this class forwards it to the command stream, the
		 *    stream puts the command in the ring, OpenGLDriver::<name> does the work where the
		 *    context is. The _RETURN form hands a value back to the caller (a fresh handle), so it
		 *    reserves the handle here and records the command that gives it its backend object
		 *    (Filament's createBufferObjectS / createBufferObjectR). */
#define DECL_DRIVER_COMMAND(name, paramsDecl, params) void Enqueue##name paramsDecl;
#define DECL_DRIVER_API(name, paramsDecl, params) void name paramsDecl;
#define DECL_DRIVER_API_RETURN(RetType, name, paramsDecl, params) RetType name paramsDecl;
#include <Rendering/Backend/DriverAPI.inc>
#undef DECL_DRIVER_COMMAND
#undef DECL_DRIVER_API
#undef DECL_DRIVER_API_RETURN

		/** The driver of the running module: Filament's `Engine::getEngine()->getDriver()`.
		 *
		 * Resource creation and every operation on the handles it returns go through it (see
		 * DriverAPI.inc). Only valid once the module owns a driver - Filament has the same
		 * requirement: there is no resource before there is an engine. */
		static Driver& Get();

		// ---- The synchronous halves of the buffer API --------------------------------------
		//
		// These two return the GPU's answer, so they are not commands: the query is recorded (it
		// has to run where the context is) and then waited for. Filament declares them
		// DECL_DRIVER_API_RETURN too (getFenceStatus / mapBuffer) and its command stream serves
		// them through getResult(); ours has no result channel, so the wait is FlushAndWait and they
		// are between-frame calls only (the HZB readback uses DownloadBuffer instead, so no frame
		// ever waits).

		bool IsBufferFenceSignaled(Rendering::Backend::ResourceHandle<Rendering::Backend::HwBufferObject> p_buffer);

		/** Filament's mapBuffer: the range is mapped where the context is and the pointer comes
		 * back. It stays mapped until UnmapBufferObject runs, so the caller reads it while the
		 * range is still mapped. Synchronous, like the fence query. */
		void* MapBufferRead(
			Rendering::Backend::ResourceHandle<Rendering::Backend::HwBufferObject> p_buffer,
			uint64_t p_offset,
			uint64_t p_size);
		/** Records p_command to be run when the batch is executed, on the render side.
		 *
		 * The escape hatch for GL that is not described by a command yet - an overlay renderer, a
		 * pass that still does its own GL. It is the migration tool (Filament calls it
		 * queueCommand) and it costs a heap allocation, so it is the exception, not the rule: what
		 * it wraps should become a command of its own (or move into the HAL/executor) as soon as
		 * it is hot. Everything the lambda touches must live until the batch runs, which is the
		 * same frame it was recorded in. */
		void Enqueue(std::function<void()> p_command);
		/** Creates a texture on the side that owns the context (Filament's createTexture): the
		 * request names it by handle and describes it; the object exists once the command runs,
		 * which is before anything recorded after it. */
		/** Updates a sub-image of a texture that already has storage. */
		/** Binds p_target as the render target (null: the default framebuffer).
		 *
		 * Recorded like every other command: the bind lands in the batch's order, before the draws
		 * that follow it (docs/RenderThreadPlan.md §8.72). */
		void BindFramebuffer(const Data::Framebuffer* p_target);





		/** Binds one descriptor - a buffer range or a texture - as a command (see
		 * Backend::BindDescriptorRequest). */
		void BindDescriptor(const ::Rendering::Backend::BindDescriptorRequest& p_request);

		/** Builds a render target's attachments on the side that owns the context. */
		void SetupFramebuffer(Data::Framebuffer& p_target, uint32_t p_width, uint32_t p_height,
			bool p_useDepth, bool p_useStencil, bool p_useMipMaps, bool p_useMultiSample);

		/** The descriptor form: the attachment's format comes from p_desc. */
		void SetupFramebuffer(Data::Framebuffer& p_target, const Settings::TextureDesc& p_desc,
			bool p_useDepth, bool p_useStencil, bool p_useMultiSample);

		/** Resizes a render target on the side that owns the context (the attachments are GL). */
		void ResizeFramebuffer(Data::Framebuffer& p_target, uint16_t p_width, uint16_t p_height);

		/** Blits one colour attachment of p_source into one of p_target (MSAA resolve, present). */
		void CopyFramebufferColor(Data::Framebuffer& p_source, uint32_t p_sourceIndex,
			Data::Framebuffer& p_target, uint32_t p_targetIndex);
		/** Depth blit between two targets (Filament's blit with TargetBufferFlags::DEPTH): the
		 * depth-peeling pass copies the opaque depth into its first layer with it. */
		void CopyFramebufferDepth(Data::Framebuffer& p_source, Data::Framebuffer& p_target);
		/** Front end: publishes what has been recorded as one batch (Filament's FEngine::flush ->
		 * flushCommandBuffer). Any thread may call it; only the recording thread may record, so
		 * in practice it is the frame's thread. Does not execute anything. */
		void Flush();
		/** Publishes what has been recorded and waits until it has run, on the side that runs it.
		 *
		 * This is what the synchronous driver calls need - a readback into CPU memory, a fence
		 * query, a mapping: they record their command and then wait for its result. It publishes a
		 * partial batch, so it is a synchronous point like resource creation used to be, and it is
		 * only meant for the calls that happen between frames (a pick, a screenshot). Commands that
		 * run during a frame use the asynchronous form instead (ReadPixelsToBuffer + DownloadBuffer). */
		void FlushAndWait();
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
		 * DriverBase::isDriverThread). Only the context-owning checks use it now; whether an
		 * operation is recorded no longer depends on the mode (docs/RenderThreadPlan.md §8.72). */
		bool IsDriverThread() const;
		/** Whether the calling thread is the one that runs the batches - and only with a render
		 * thread: in the single-threaded configuration the recording thread is not the executor
		 * until Execute() is called. */
		bool IsExecutorThread() const;
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
		 * space back. This is the body of the render thread's loop (Filament's FEngine::execute:
		 * waitForCommands -> driver.execute -> releaseBuffer); without a render thread it runs on
		 * the recording thread, at Driver::Execute. */
		void ExecuteAll();
		/** Back end, blocking: waits for the next published batch, runs it, gives the ring back.
		 * False means the queue was asked to exit (Filament's FEngine::execute, which returns
		 * false to break the render thread's loop). */
		bool WaitAndExecute();
		/** Blocks until the batch that produced frame p_frameIndex has been drawn.
		 *
		 * The render thread passes its GPU fence before it counts a frame as completed (see
		 * OpenGLDriver::WaitForFrameCompletion), so this is "the frame is finished", not "its
		 * commands were issued". It is what lets the window present frame N-1 while frame N is
		 * still being drawn - the swap chain's shape in the Qt/Filament demo, where the consumer
		 * waits for the *previous* frame's commit instead of stalling on the current one
		 * (docs/RenderThreadPlan.md §8.69).
		 *
		 * Single-threaded, execution already happened when the frame was recorded, so this
		 * returns at once. */
		void WaitForFrameCompleted(uint64_t p_frameIndex);
		/** How many frames the executor has finished (drawn). */
		uint64_t GetCompletedFrameCount() const;
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
		bool Execute();
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
		bool m_renderThreadActive = false;
		/** The thread the batches run on. Execute() compares against it to decide whether this call
		 * is the driver loop or a publish-and-wait (Filament's isThisThreadTheDriverThread). */
		std::thread::id m_renderThreadId{};
		/** Frames the executor has finished drawing, and the lock the consumer waits under. The
		 * executor counts a frame only after its GPU fence, so this is "drawn", not "issued". */
		mutable std::mutex m_frameMutex;
		std::condition_variable m_frameCondition;
		uint64_t m_frameCompleted = 0;
	};
}
