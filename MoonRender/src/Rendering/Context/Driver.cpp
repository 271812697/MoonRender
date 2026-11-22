
#include <Rendering/Context/Driver.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>


#include <cstdlib>
#include <cstring>
#include <iostream>
#include <Rendering/Backend/CommandBufferQueue.h>
#include <Rendering/Backend/CommandStream.h>
#include <Rendering/Backend/DeferredDeleter.h>
#include <Rendering/Backend/BackendResources.h>
#include <Rendering/Backend/DrawMeshRequest.h>
#include <Rendering/Backend/BindFramebufferRequest.h>
#include <Rendering/Backend/OpenGLDriver.h>
#include <Rendering/HAL/Backend.h>
#include <Rendering/Utils/Conversions.h>
#include <tracy/Tracy.hpp>

namespace
{
	std::unique_ptr<Rendering::HAL::Backend> m_gfxBackend;

	/** The ring the frame is recorded into, and the space a publish is guaranteed after a flush.
	 *
	 * Sized for a whole frame, like Filament's command buffer: a frame is tens of KB to a few MB of
	 * commands, and recording is append-only - nothing runs from inside it any more, so the ring has
	 * to hold everything between two Execute() calls (docs/RenderThreadPlan.md §8.72). 64 KB was not
	 * enough, and a full ring used to have to run what it had, mid-frame. */
	constexpr size_t kRequiredCommandBytes = 1024 * 1024;
	constexpr size_t kCommandRingBytes = 8 * 1024 * 1024;
}

Rendering::Context::Driver::Driver(const Rendering::Settings::DriverSettings& p_driverSettings)
{
	m_gfxBackend = std::make_unique<Rendering::HAL::Backend>();

	auto initialPipelineState = m_gfxBackend->Init(p_driverSettings.debugMode);

	// The command path first: the pipeline state below is applied *through* it, so the queue, its
	// writer and the executor have to exist before anything is recorded.
	m_openGLDriver = std::make_unique<Rendering::Backend::OpenGLDriver>(*m_gfxBackend);
	m_commandQueue = std::make_unique<Rendering::Backend::CommandBufferQueue>(kRequiredCommandBytes, kCommandRingBytes);
	m_commandStream = std::make_unique<Rendering::Backend::CommandStream>(*m_commandQueue);

	if (p_driverSettings.defaultPipelineState)
	{
		m_defaultPipelineState = p_driverSettings.defaultPipelineState.value();
	}

	m_pipelineState = initialPipelineState.value();
	// The backend is in the state its Init returned; the executor's diff has to start from there.
	m_openGLDriver->ResetAppliedState(m_pipelineState);
	SetPipelineState(m_defaultPipelineState);

	// From here on there is a context for GL objects to be destroyed on, so a resource released
	// asynchronously can be queued instead of deleted where it is dropped.
	Rendering::Backend::DeferredDeleter::SetActive(true);

	m_vendor = m_gfxBackend->GetVendor();
	m_hardware = m_gfxBackend->GetHardware();
	m_version = m_gfxBackend->GetVersion();
	m_shadingLanguageVersion = m_gfxBackend->GetShadingLanguageVersion();
}

Rendering::Context::Driver::~Driver()
{
	// What was still queued for deletion is released now, while the context the driver belongs to
	// is still current (the owner destroys the driver with it current). After this there is no
	// context to defer to any more, so later releases destroy where they happen.
	Rendering::Backend::DeferredDeleter::Flush();
	Rendering::Backend::DeferredDeleter::SetActive(false);
}

/** Expands a parenthesised argument list (the `params` column of DriverAPI.inc) into separate
 * arguments, so it can follow `*this,` in the generated recording stubs. */
#define MOON_EXPAND_ARGS(...) __VA_ARGS__

#define DECL_DRIVER_COMMAND(name, paramsDecl, params) \
	void Rendering::Context::Driver::Enqueue##name paramsDecl \
	{ \
		/* Recording lives on the command stream and is the same in both configurations: the */ \
		/* executor is the render thread, or this thread at the end of the frame (see */ \
		/* ExecuteAll). Nothing runs inline here any more (docs/RenderThreadPlan.md §8.72). */ \
		m_commandStream->name(MOON_EXPAND_ARGS params); \
	}
#include <Rendering/Backend/DriverAPI.inc>
#undef DECL_DRIVER_COMMAND
#undef MOON_EXPAND_ARGS

void Rendering::Context::Driver::Enqueue(std::function<void()> p_command)
{
	m_commandStream->Enqueue<::Rendering::Backend::CustomCommand>(std::move(p_command));
}

void Rendering::Context::Driver::ReadPixels(const ::Rendering::Backend::ReadPixelsRequest& p_request)
{
	m_commandStream->ReadPixels(p_request);
}

void Rendering::Context::Driver::ApplyVertexArrayLayout(const ::Rendering::Backend::SetVertexArrayLayoutRequest& p_request)
{
	m_commandStream->SetVertexArrayLayout(p_request);
}

void Rendering::Context::Driver::CreateProgram(::Rendering::Backend::CreateProgramRequest p_request)
{
	if (p_request.programHandleId == 0)
	{
		return;
	}

	m_commandStream->CreateProgram(std::move(p_request));
}

void Rendering::Context::Driver::BindProgram(uint32_t p_programId)
{
	m_commandStream->BindProgram(p_programId);
}

void Rendering::Context::Driver::BindProgramHandle(uint32_t p_programHandleId)
{
	m_commandStream->BindProgramHandle(p_programHandleId);
}

void Rendering::Context::Driver::DeclareProgramBindings(::Rendering::Backend::DeclareProgramBindingsRequest p_request)
{
	if (p_request.programHandleId == 0 || p_request.bindings == nullptr)
	{
		return;
	}

	m_commandStream->DeclareProgramBindings(std::move(p_request));
}

void Rendering::Context::Driver::UpdateBufferRange(
	uint32_t p_bufferId, uint32_t p_offset, const void* p_data, uint32_t p_size)
{
	if (p_bufferId == 0 || p_data == nullptr || p_size == 0)
	{
		return;
	}

	if (std::getenv("MOON_FRAME_TRACE") != nullptr)
	{
		static int calls = 0;
		if (++calls < 40 || (calls % 500) == 0)
		{
			std::fprintf(stderr, "[buf] #%d id=%u off=%u size=%u\n", calls, p_bufferId, p_offset, p_size);
			std::fflush(stderr);
		}
	}
	// The bytes are owned by the command (Filament's BufferDescriptor), not by the ring: an upload
	// can be megabytes, and the ring is sized for a batch of commands.
	auto owned = std::make_shared<std::vector<uint8_t>>(p_size);
	std::memcpy(owned->data(), p_data, p_size);

	::Rendering::Backend::UpdateBufferRequest request;
	request.bufferId = p_bufferId;
	request.offset = p_offset;
	request.size = p_size;
	request.data = std::move(owned);
	m_commandStream->UpdateBuffer(request);
}

void Rendering::Context::Driver::BindDescriptor(const ::Rendering::Backend::BindDescriptorRequest& p_request)
{
	m_commandStream->BindDescriptor(p_request);
}

void Rendering::Context::Driver::SetupFramebuffer(HAL::Framebuffer& p_target, uint32_t p_width, uint32_t p_height,
	bool p_useDepth, bool p_useStencil, bool p_useMipMaps, bool p_useMultiSample)
{
	if (std::getenv("MOON_FRAME_TRACE") != nullptr)
	{
		std::fprintf(stderr, "[setup] record target=%p name=%s %ux%u\n",
			static_cast<void*>(&p_target), p_target.GetDebugName().c_str(), p_width, p_height);
		std::fflush(stderr);
	}	::Rendering::Backend::SetupFramebufferRequest request;
	request.target = &p_target;
	request.width = p_width;
	request.height = p_height;
	request.useDepth = p_useDepth;
	request.useStencil = p_useStencil;
	request.useMipMaps = p_useMipMaps;
	request.useMultiSample = p_useMultiSample;
	m_commandStream->SetupFramebuffer(request);
}

void Rendering::Context::Driver::SetupFramebuffer(HAL::Framebuffer& p_target, const ::Rendering::Settings::TextureDesc& p_desc,
	bool p_useDepth, bool p_useStencil, bool p_useMultiSample)
{
	::Rendering::Backend::SetupFramebufferRequest request;
	request.target = &p_target;
	request.width = p_desc.width;
	request.height = p_desc.height;
	request.useDepth = p_useDepth;
	request.useStencil = p_useStencil;
	request.useMultiSample = p_useMultiSample;
	request.useDesc = true;
	request.desc = p_desc;
	m_commandStream->SetupFramebuffer(request);
}

void Rendering::Context::Driver::ResizeFramebuffer(HAL::Framebuffer& p_target, uint16_t p_width, uint16_t p_height)
{
	::Rendering::Backend::ResizeFramebufferRequest request;
	request.target = &p_target;
	request.width = p_width;
	request.height = p_height;
	m_commandStream->ResizeFramebuffer(request);
}

void Rendering::Context::Driver::CopyFramebufferColor(
	HAL::Framebuffer& p_source, uint32_t p_sourceIndex,
	HAL::Framebuffer& p_target, uint32_t p_targetIndex)
{
	::Rendering::Backend::CopyFramebufferColorRequest request;
	request.source = &p_source;
	request.sourceIndex = p_sourceIndex;
	request.target = &p_target;
	request.targetIndex = p_targetIndex;
	m_commandStream->CopyFramebufferColor(request);
}

void Rendering::Context::Driver::CopyFramebufferDepth(
	HAL::Framebuffer& p_source, HAL::Framebuffer& p_target)
{
	::Rendering::Backend::CopyFramebufferDepthRequest request;
	request.source = &p_source;
	request.target = &p_target;
	m_commandStream->CopyFramebufferDepth(request);
}

void Rendering::Context::Driver::BindFramebuffer(const HAL::Framebuffer* p_target)
{
	::Rendering::Backend::BindFramebufferRequest request;
	request.target = p_target;
	EnqueueBindFramebuffer(request);
}

void Rendering::Context::Driver::CreateTexture(Rendering::Backend::CreateTextureRequest p_request)
{
	if (p_request.handleId == 0)
	{
		return;
	}

	EnqueueCreateTexture(std::move(p_request));
}

void Rendering::Context::Driver::UpdateTexture(Rendering::Backend::UpdateTextureRequest p_request)
{
	if (p_request.handleId == 0 || p_request.pixels.empty())
	{
		return;
	}

	EnqueueUpdateTexture(std::move(p_request));
}

void Rendering::Context::Driver::UploadTexture(Rendering::Backend::TextureUploadRequest p_request)
{
	if (p_request.handleId == 0 || p_request.pixels.empty())
	{
		return;
	}

	// The command carries the descriptor and the bytes; the executor runs it where the context
	// lives. Nothing is left here but the recording.
	EnqueueUploadTexture(std::move(p_request));
}

void Rendering::Context::Driver::Flush()
{
	// Front end: publish what has been recorded. Nothing to publish, nothing to do (the queue
	// skips empty flushes the same way - Filament's flushCommandBuffer does).
	if (m_commandQueue->GetCircularBuffer().IsEmpty())
	{
		return;
	}
	m_commandQueue->Flush();
}

void Rendering::Context::Driver::ExecuteAll()
{
	// Resources whose last reference was dropped asynchronously (a mesh freed by a feature rebuild
	// or a job thread) are destroyed here: this is a point where the module's context is current
	// and the batch that could still have referenced them has just been replayed.
	Rendering::Backend::DeferredDeleter::Flush();

	// Back end: take the published batches, run them, give the ring back. This is the body of the
	// render thread's loop (Filament's FEngine::execute) - it just happens to run on the recording
	// thread when there is no render thread.
	//
	// The ring being empty does not mean there is nothing to run: Flush() may already have
	// published a batch, and that batch is exactly what this has to execute. Only the batches
	// themselves say whether there is work.
	if (!m_commandQueue->GetCircularBuffer().IsEmpty())
	{
		m_commandQueue->Flush();
	}
	const std::vector<Rendering::Backend::CircularBuffer::Range> batches = m_commandQueue->TakeCommands();
	if (batches.empty())
	{
		return;
	}
	for (const Rendering::Backend::CircularBuffer::Range& batch : batches)
	{
		m_openGLDriver->Execute(batch.tail, batch.head);
	}
	m_commandQueue->ReleaseBuffers(batches);
}

void Rendering::Context::Driver::SetRenderThreadActive(bool p_active)
{
	m_renderThreadActive = p_active;
	if (p_active)
	{
		// This is called from the thread that will run the batches (RenderModuleHost::Loop): that
		// is the id ExecutePendingCommands compares against, and getting it wrong deadlocks - a
		// command that drains would wait for its own batch.
		m_renderThreadId = std::this_thread::get_id();
	}
}

bool Rendering::Context::Driver::IsDriverThread() const
{
	// No render thread means this thread is the one and only: it owns the context, so GL is its
	// business. With one, only that thread may issue GL - everyone else records.
	return !m_renderThreadActive || std::this_thread::get_id() == m_renderThreadId;
}

bool Rendering::Context::Driver::WaitAndExecute()
{
	// The render thread's body (Filament's FEngine::execute): block until a batch is published, run
	// it, give the ring back. False means the queue was asked to exit.
	const std::vector<Rendering::Backend::CircularBuffer::Range> batches = m_commandQueue->WaitForCommands();
	if (batches.empty())
	{
		return false;
	}

	Rendering::Backend::DeferredDeleter::Flush();
	for (const Rendering::Backend::CircularBuffer::Range& batch : batches)
	{
		m_openGLDriver->Execute(batch.tail, batch.head);
	}

	// The batch is not "done" when its commands have been *issued*: the GPU is still drawing. The
	// consumer side waits for this batch before it presents (Driver::Execute -> WaitForIdle), and
	// without a fence that wait means "recorded and issued", so the paste can race the frame it is
	// showing - which is the display trailing the camera by a frame. Fence first, then hand the ring
	// back: that makes the consumer's wait mean "this frame is drawn"
	// (docs/RenderThreadPlan.md §8.69).
	m_openGLDriver->WaitForFrameCompletion();

	// The frame is drawn now: say so, so the consumer can present it (frame N-1 while N is being
	// drawn, exactly as the Qt/Filament demo does with its swap chain ring).
	{
		std::lock_guard lock(m_frameMutex);
		++m_frameCompleted;
	}
	m_frameCondition.notify_all();

	m_commandQueue->ReleaseBuffers(batches);
	return true;
}

void Rendering::Context::Driver::WaitForFrameCompleted(uint64_t p_frameIndex)
{
	// Single-threaded, the frame was executed while it was recorded: there is nothing to wait for.
	if (!m_renderThreadActive || p_frameIndex == 0)
	{
		return;
	}
	std::unique_lock lock(m_frameMutex);
	m_frameCondition.wait(lock, [this, p_frameIndex] { return m_frameCompleted >= p_frameIndex; });
}

uint64_t Rendering::Context::Driver::GetCompletedFrameCount() const
{
	std::lock_guard lock(m_frameMutex);
	return m_frameCompleted;
}

void Rendering::Context::Driver::MakeCurrent()
{
	// The frame's first command, exactly as Filament issues it (FRenderer::beginFrame ->
	// FSwapChain::makeCurrent -> driver.makeCurrent): the executor makes the module's context
	// current for the batch it is about to run.
	m_commandStream->MakeCurrent();
}

void Rendering::Context::Driver::RequestExit()
{
	m_commandQueue->RequestExit();
}

void Rendering::Context::Driver::Execute()
{
	// Filament's Engine::execute.
	Flush();

	// The recording side with a render thread: publishing is all it can do. The batch belongs to
	// the other thread; waiting is what makes the frame readable right after the call, which is
	// what the consumer (the paste) needs.
	if (m_renderThreadActive && std::this_thread::get_id() != m_renderThreadId)
	{
		m_commandQueue->WaitForIdle();
		return;
	}

	// No render thread: this call is the driver loop.
	ExecuteAll();
}

void Rendering::Context::Driver::ExecutePendingCommands()
{
	// Flush + wait, the same body: Filament spells the two cases flushAndWait and execute, and the
	// difference that matters to our callers is whether they read a result back immediately.
	Execute();
}

Rendering::Backend::CommandBufferQueue& Rendering::Context::Driver::GetCommandBufferQueue()
{
	return *m_commandQueue;
}

Rendering::Backend::CommandStream& Rendering::Context::Driver::GetCommandStream()
{
	return *m_commandStream;
}

void Rendering::Context::Driver::SetContextHost(IGLContextHost* p_contextHost)
{
	m_contextHost = p_contextHost;
	// The executor is what calls makeCurrent (Filament's OpenGLDriver::makeCurrent), so it needs
	// the host too.
	if (m_openGLDriver != nullptr)
	{
		m_openGLDriver->SetContextHost(p_contextHost);
	}
}

Rendering::Context::IGLContextHost* Rendering::Context::Driver::GetContextHost() const
{
	return m_contextHost;
}
void Rendering::Context::Driver::ReapplyGlobalState()
{
	// Init also (re)loads the GL entry points, which keeps the pointers valid for the context
	// that is current now. It puts the backend back into the state it started in, so the
	// executor has to forget what it thinks is applied - otherwise the next diff skips settings
	// GL no longer has.
	const auto initialState = m_gfxBackend->Init(false);
	if (initialState.has_value())
	{
		m_openGLDriver->ResetAppliedState(*initialState);
	}
}

void Rendering::Context::Driver::OnFrameCompleted()
{
	m_gfxBackend->OnFrameCompleted();

	// Prevents state leak between frames, and especially useful when external code (like ImGui)
	// requires a "neutral" pipeline state.
	ResetPipelineState();
}

void Rendering::Context::Driver::SetViewport(uint32_t p_x, uint32_t p_y, uint32_t p_width, uint32_t p_height)
{
	// A command, like everything else the frame issues: recorded here, applied by the executor in
	// the batch's order (docs/RenderThreadPlan.md §8.72).
	EnqueueViewport(
		static_cast<int32_t>(p_x),
		static_cast<int32_t>(p_y),
		static_cast<int32_t>(p_width),
		static_cast<int32_t>(p_height));
}

void Rendering::Context::Driver::Clear(
	bool p_colorBuffer,
	bool p_depthBuffer,
	bool p_stencilBuffer,
	const Maths::FVector4& p_color,
	bool p_scissor
)
{
	// The clear, the colour it uses and the state it needs are one batch: nothing outside them
	// touches GL in between, so they can be recorded and executed together.
	if (p_colorBuffer)
	{
		EnqueueSetClearColor(p_color.x, p_color.y, p_color.z, p_color.w);
	}

	auto pso = CreatePipelineState();

	if (p_stencilBuffer)
	{
		pso.stencilWriteMask = ~0;
	}

	pso.scissorTest = p_scissor;

	EnqueuePipelineState(pso);
	EnqueueClear(p_colorBuffer, p_depthBuffer, p_stencilBuffer);
}

void Rendering::Context::Driver::SetScissor(
	uint32_t p_x,
	uint32_t p_y,
	uint32_t p_width,
	uint32_t p_height
)
{
	EnqueueScissor(
		static_cast<int32_t>(p_x),
		static_cast<int32_t>(p_y),
		static_cast<int32_t>(p_width),
		static_cast<int32_t>(p_height));
}

void Rendering::Context::Driver::Draw(
	Data::PipelineState p_pso,
	const Resources::IMesh& p_mesh,
	Settings::EPrimitiveMode p_primitiveMode,
	uint32_t p_instances
)
{
	Draw(p_pso,p_mesh,0,p_primitiveMode,p_instances);
}

void Rendering::Context::Driver::Draw(Rendering::Data::PipelineState p_pso, const Resources::IMesh& p_mesh, int index, Settings::EPrimitiveMode p_primitiveMode, uint32_t p_instances)
{
	ZoneScoped;
	if (p_instances == 0)
	{
		return;
	}

	// One draw is a pipeline state, the array binding and the draw call, recorded in that order.
	// The per-material binding (program, textures, UBOs) is recorded immediately before this call,
	// so the whole drawable is a run of commands in the frame's batch.
	EnqueuePipelineState(p_pso);

	::Rendering::Backend::DrawMeshRequest request;
	request.mesh = &p_mesh;
	request.subIndex = index;
	request.primitiveMode = p_primitiveMode;
	request.instances = p_instances;
	EnqueueDrawMesh(request);
}

void Rendering::Context::Driver::SetPipelineState(Rendering::Data::PipelineState p_state)
{
	// The state machine itself lives in the executor (OpenGLDriver::PipelineState): it diffs
	// against what the backend is in, and that cache belongs to whichever thread owns the
	// context. Here the state is only recorded.
	EnqueuePipelineState(p_state);
}

void Rendering::Context::Driver::ResetPipelineState()
{
	SetPipelineState(m_defaultPipelineState);
}

Rendering::Data::PipelineState Rendering::Context::Driver::CreatePipelineState() const
{
	return m_defaultPipelineState;
}

std::string_view Rendering::Context::Driver::GetVendor() const
{
	return m_vendor;
}

std::string_view Rendering::Context::Driver::GetHardware() const
{
	return m_hardware;
}

std::string_view Rendering::Context::Driver::GetVersion() const
{
	return m_version;
}

std::string_view Rendering::Context::Driver::GetShadingLanguageVersion() const
{
	return m_shadingLanguageVersion;
}
