
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

	/** Space a batch is guaranteed after a flush, and the ring itself: large enough that a frame
	 * of commands (today a few hundred bytes) never fills it, so flush never blocks when the
	 * executor runs on the same thread. */
	constexpr size_t kRequiredCommandBytes = 8 * 1024;
	constexpr size_t kCommandRingBytes = 64 * 1024;
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
	// Single-threaded the executor is this thread, so a full ring is drained from here; with a
	// render thread publishing is enough and the queue blocks the producer until space comes back.

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

namespace
{
	/** Room kept free for one command, so a record never has to be split. */
	constexpr size_t kCommandReserveBytes = Rendering::Backend::CommandBase::Align(256);

	template <typename TCommand, typename... A>
	void Record(Rendering::Context::Driver& p_driver, A&&... p_arguments)
	{
		Rendering::Backend::CommandBufferQueue& queue = p_driver.GetCommandBufferQueue();
		const Rendering::Backend::CircularBuffer& ring = queue.GetCircularBuffer();
		if (ring.Size() - ring.GetUsed() < kCommandReserveBytes)
		{
			// Long batches stay safe: what is recorded so far is executed before the ring is
			// reused. (With the frame on its own thread this becomes a wait for the render thread
			// to release a batch instead.)
			p_driver.ExecutePendingCommands();
		}
		p_driver.GetCommandStream().Enqueue<TCommand>(std::forward<A>(p_arguments)...);
	}
}

/** Expands a parenthesised argument list (the `params` column of DriverAPI.inc) into separate
 * arguments, so it can follow `*this,` in the generated recording stubs. */
#define MOON_EXPAND_ARGS(...) __VA_ARGS__

#define DECL_DRIVER_COMMAND(name, paramsDecl, params) \
	void Rendering::Context::Driver::Enqueue##name paramsDecl \
	{ \
		if (m_executing) \
		{ \
			/* Already on the render side, inside a batch: run it now. Recording would append to \
			 * the ring the executor is walking (see Driver::Enqueue). */ \
			m_openGLDriver->name(MOON_EXPAND_ARGS params); \
			return; \
		} \
		/* Recording lives on the command stream; this side only decides inline vs queued. */ \
		m_commandStream->name(MOON_EXPAND_ARGS params); \
	}
#include <Rendering/Backend/DriverAPI.inc>
#undef DECL_DRIVER_COMMAND
#undef MOON_EXPAND_ARGS

void Rendering::Context::Driver::Enqueue(std::function<void()> p_command)
{
	if (m_executing)
	{
		// Nested use: the batch is running, so this runs with it.
		p_command();
		return;
	}
	m_commandStream->Enqueue<::Rendering::Backend::CustomCommand>(std::move(p_command));
}

void Rendering::Context::Driver::ReadPixels(const ::Rendering::Backend::ReadPixelsRequest& p_request)
{
	if (m_executing)
	{
		m_openGLDriver->ReadPixels(p_request);
		return;
	}
	m_commandStream->ReadPixels(p_request);
}

void Rendering::Context::Driver::ApplyVertexArrayLayout(const ::Rendering::Backend::SetVertexArrayLayoutRequest& p_request)
{
	if (m_executing)
	{
		m_openGLDriver->SetVertexArrayLayout(p_request);
		return;
	}
	m_commandStream->SetVertexArrayLayout(p_request);
}

void Rendering::Context::Driver::CreateProgram(::Rendering::Backend::CreateProgramRequest p_request)
{
	if (p_request.programHandleId == 0)
	{
		return;
	}

	if (m_executing)
	{
		m_openGLDriver->CreateProgram(std::move(p_request));
		return;
	}
	m_commandStream->CreateProgram(std::move(p_request));
}

void Rendering::Context::Driver::BindProgram(uint32_t p_programId)
{
	if (m_executing)
	{
		m_openGLDriver->BindProgram(p_programId);
		return;
	}
	m_commandStream->BindProgram(p_programId);
}

void Rendering::Context::Driver::BindProgramHandle(uint32_t p_programHandleId)
{
	if (m_executing)
	{
		m_openGLDriver->BindProgramHandle(p_programHandleId);
		return;
	}
	m_commandStream->BindProgramHandle(p_programHandleId);
}

void Rendering::Context::Driver::SetSamplerUnit(uint32_t p_programId, std::string p_samplerName, uint32_t p_unit)
{
	if (p_programId == 0)
	{
		return;
	}

	if (m_executing)
	{
		m_openGLDriver->SetSamplerUnit(p_programId, std::move(p_samplerName), p_unit);
		return;
	}
	m_commandStream->SetSamplerUnit(p_programId, std::move(p_samplerName), p_unit);
}

void Rendering::Context::Driver::DeclareProgramBindings(::Rendering::Backend::DeclareProgramBindingsRequest p_request)
{
	if (p_request.programHandleId == 0 || p_request.bindings == nullptr)
	{
		return;
	}

	if (m_executing)
	{
		m_openGLDriver->DeclareProgramBindings(std::move(p_request));
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
	if (m_executing)
	{
		m_openGLDriver->UpdateBuffer(request);
		return;
	}
	m_commandStream->UpdateBuffer(request);
}

void Rendering::Context::Driver::BindDescriptor(const ::Rendering::Backend::BindDescriptorRequest& p_request)
{
	if (m_executing)
	{
		m_openGLDriver->BindDescriptor(p_request);
		return;
	}
	m_commandStream->BindDescriptor(p_request);
}

void Rendering::Context::Driver::SetupFramebuffer(HAL::Framebuffer& p_target, uint32_t p_width, uint32_t p_height,
	bool p_useDepth, bool p_useStencil, bool p_useMipMaps, bool p_useMultiSample)
{
	if (std::getenv("MOON_FRAME_TRACE") != nullptr)
	{
		std::fprintf(stderr, "[setup] record target=%p name=%s %ux%u executing=%d\n",
			static_cast<void*>(&p_target), p_target.GetDebugName().c_str(), p_width, p_height,
			m_executing ? 1 : 0);
		std::fflush(stderr);
	}	::Rendering::Backend::SetupFramebufferRequest request;
	request.target = &p_target;
	request.width = p_width;
	request.height = p_height;
	request.useDepth = p_useDepth;
	request.useStencil = p_useStencil;
	request.useMipMaps = p_useMipMaps;
	request.useMultiSample = p_useMultiSample;
	if (m_executing)
	{
		m_openGLDriver->SetupFramebuffer(request);
		return;
	}
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
	if (m_executing)
	{
		m_openGLDriver->SetupFramebuffer(request);
		return;
	}
	m_commandStream->SetupFramebuffer(request);
}

void Rendering::Context::Driver::ResizeFramebuffer(HAL::Framebuffer& p_target, uint16_t p_width, uint16_t p_height)
{
	::Rendering::Backend::ResizeFramebufferRequest request;
	request.target = &p_target;
	request.width = p_width;
	request.height = p_height;
	if (m_executing)
	{
		m_openGLDriver->ResizeFramebuffer(request);
		return;
	}
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
	if (m_executing)
	{
		m_openGLDriver->CopyFramebufferColor(request);
		return;
	}
	m_commandStream->CopyFramebufferColor(request);
}

void Rendering::Context::Driver::CopyFramebufferDepth(
	HAL::Framebuffer& p_source, HAL::Framebuffer& p_target)
{
	::Rendering::Backend::CopyFramebufferDepthRequest request;
	request.source = &p_source;
	request.target = &p_target;
	if (m_executing)
	{
		m_openGLDriver->CopyFramebufferDepth(request);
		return;
	}
	m_commandStream->CopyFramebufferDepth(request);
}

void Rendering::Context::Driver::BindFramebuffer(const HAL::Framebuffer* p_target)
{
	::Rendering::Backend::BindFramebufferRequest request;
	request.target = p_target;
	if (m_executing)
	{
		m_openGLDriver->BindFramebuffer(request);
		return;
	}
	EnqueueBindFramebuffer(request);
	// Scaffolding, like SetPipelineState: the passes still issue immediate GL right after the
	// bind, so it has to land before them (see DriverAPI.inc).
	ExecutePendingCommands();
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
	// Inside a batch there is nothing to publish: the ring is the batch being walked, and handing it
	// to the queue would publish a partial one. Filament's flush is a no-op on the driver thread for
	// the same reason.
	if (m_executing)
	{
		return;
	}

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
	// Already running a batch: everything recorded now runs as part of that walk, so there is
	// nothing to execute here - re-entering would walk the same ring twice.
	if (m_executing)
	{
		return;
	}

	// Resources whose last reference was dropped asynchronously (a mesh freed by a feature rebuild
	// or a job thread) are destroyed here: this is a point where the module's context is current
	// and the batch that could still have referenced them has just been replayed.
	Rendering::Backend::DeferredDeleter::Flush();

	// Back end: take the published batches, run them, give the ring back. This is the body of the
	// render thread's loop (Filament's FEngine::execute) - it just happens to run on the recording
	// thread until the frame moves.
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
		// While a batch runs, everything the driver is asked for happens inside it (see the
		// recording stubs and Enqueue): recording from there would append to the ring the executor
		// is walking.
		m_executing = true;
		m_openGLDriver->Execute(batch.tail, batch.head);
		m_executing = false;
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
		m_executing = true;
		m_openGLDriver->Execute(batch.tail, batch.head);
		m_executing = false;
	}
	m_commandQueue->ReleaseBuffers(batches);
	return true;
}

void Rendering::Context::Driver::MakeCurrent()
{
	// The frame's first command, exactly as Filament issues it (FRenderer::beginFrame ->
	// FSwapChain::makeCurrent -> driver.makeCurrent): the executor makes the module's context
	// current for the batch it is about to run.
	if (m_executing)
	{
		m_openGLDriver->MakeCurrent();
		return;
	}
	m_commandStream->MakeCurrent();
}

void Rendering::Context::Driver::RequestExit()
{
	m_commandQueue->RequestExit();
}

void Rendering::Context::Driver::Execute()
{
	// Filament's Engine::execute.
	//
	// Inside a batch there is nothing to execute: what is recorded now runs as part of the walk
	// this call is already in. Without this, a command that drains would wait for the batch that is
	// currently running - on the render thread that is a wait on itself.
	if (m_executing)
	{
		return;
	}

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
	{
		static const bool trace = std::getenv("MOON_FRAME_TRACE") != nullptr;
		if (trace)
		{
			std::fprintf(stderr, "[drv] viewport enter executing=%d active=%d same=%d\n", m_executing ? 1 : 0, m_renderThreadActive ? 1 : 0, (std::this_thread::get_id() == m_renderThreadId) ? 1 : 0);
			std::fflush(stderr);
		}
	}
	// A command, like everything else the frame issues. Passes set the viewport right before they
	// bind textures and draw, so it is executed immediately after being recorded (scaffolding,
	// same reason as SetPipelineState - see DriverAPI.inc).
	EnqueueViewport(
		static_cast<int32_t>(p_x),
		static_cast<int32_t>(p_y),
		static_cast<int32_t>(p_width),
		static_cast<int32_t>(p_height));
	{ static const bool t = std::getenv("MOON_FRAME_TRACE") != nullptr; if (t) { std::fprintf(stderr, "[drv] enqueued\n"); std::fflush(stderr); } }
	ExecutePendingCommands();
	{ static const bool t = std::getenv("MOON_FRAME_TRACE") != nullptr; if (t) { std::fprintf(stderr, "[drv] viewport executed\n"); std::fflush(stderr); } }
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
	if (!m_executing)
	{
		ExecutePendingCommands();
	}
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
	{ static const bool t = std::getenv("MOON_FRAME_TRACE") != nullptr; if (t) { std::fprintf(stderr, "[drv] enqueued\n"); std::fflush(stderr); } }
	ExecutePendingCommands();
	{ static const bool t = std::getenv("MOON_FRAME_TRACE") != nullptr; if (t) { std::fprintf(stderr, "[drv] viewport executed\n"); std::fflush(stderr); } }
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

	// One draw is one batch: the state, the array binding and the draw call travel together and
	// are executed as a block, which is where the GL calls used to be. The per-material binding
	// (program, textures, UBOs) still happens immediately *before* this call, which is why the
	// batch is drained here instead of once per frame - see DriverAPI.inc.
	EnqueuePipelineState(p_pso);

	::Rendering::Backend::DrawMeshRequest request;
	request.mesh = &p_mesh;
	request.subIndex = index;
	request.primitiveMode = p_primitiveMode;
	request.instances = p_instances;
	EnqueueDrawMesh(request);

	if (!m_executing)
	{
		ExecutePendingCommands();
	}
}

void Rendering::Context::Driver::SetPipelineState(Rendering::Data::PipelineState p_state)
{
	// The state machine itself lives in the executor (OpenGLDriver::PipelineState): it diffs
	// against what the backend is in, and that cache belongs to whichever thread owns the
	// context. Here the state is only recorded.
	EnqueuePipelineState(p_state);

	// Scaffolding for the step between M1 and M2: the draw calls are still immediate, so the
	// state has to be applied before the next GL call rather than at the end of the frame. It
	// goes away when Driver::Draw records its binding and draw calls too, and a frame becomes one
	// batch with one drain.
	if (!m_executing)
	{
		ExecutePendingCommands();
	}
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
