
#include <Rendering/Context/Driver.h>

#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <Rendering/Backend/CommandBufferQueue.h>
#include <Rendering/Backend/CommandStream.h>
#include <Rendering/Backend/DeferredDeleter.h>
#include <Rendering/Backend/BackendResources.h>
#include <Rendering/Backend/DrawMeshRequest.h>
#include <Rendering/Backend/OpenGLDriver.h>
#include <Rendering/HAL/Backend.h>
#include <Rendering/HAL/Buffer.h>
#include <Rendering/Utils/Conversions.h>
#include <tracy/Tracy.hpp>

namespace
{
	std::unique_ptr<Rendering::HAL::Backend> m_gfxBackend;

	/** The driver of the running module (see Driver::Get). Set by the constructor and cleared by
	 * the destructor: there is one module, so there is one of these. Atomic because with a render
	 * thread the driver is made on that thread and asked for from the recording one. */
	std::atomic<Rendering::Context::Driver*> s_currentDriver{ nullptr };

	using BufferHandle = Rendering::Backend::ResourceHandle<Rendering::Backend::HwBufferObject>;

	/** The backend object a handle names, on the side that owns the context: null until the create
	 * command has run, or after the object was released. */
	Rendering::HAL::Buffer* ResolveBuffer(BufferHandle p_handle)
	{
		return Rendering::Backend::GetHandleAllocator().HandleCast<Rendering::HAL::Buffer>(p_handle);
	}

	/** The ring the frame is recorded into, and the space a publish is guaranteed after a flush.
	 *
	 * Sized for a whole frame, like Filament's command buffer: a frame is tens of KB to a few MB of
	 * commands, and recording is append-only - nothing runs from inside it any more, so the ring has
	 * to hold everything between two Execute() calls (docs/RenderThreadPlan.md §8.72). 64 KB was not
	 * enough, and a full ring used to have to run what it had, mid-frame. */
	constexpr size_t kRequiredCommandBytes = 1024 * 1024;
	constexpr size_t kCommandRingBytes = 8 * 1024 * 1024;
}

Rendering::Context::Driver& Rendering::Context::Driver::Get()
{
	Rendering::Context::Driver* const driver = s_currentDriver.load(std::memory_order_acquire);
	assert(driver != nullptr && "No render module is running: there is no driver to make or use a resource");
	return *driver;
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

	// From here on resources can be made: everything that creates or uses a handle asks for this
	// driver (see Driver::Get). Set last, so a half-built driver is never handed out.
	s_currentDriver.store(this, std::memory_order_release);
}

Rendering::Context::Driver::~Driver()
{
	if (s_currentDriver.load(std::memory_order_acquire) == this)
	{
		s_currentDriver.store(nullptr, std::memory_order_release);
	}

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

/* The driver API (Filament's DECL_DRIVER_API): the method keeps its name and forwards to the
 * command stream, which is where the command is actually recorded. Nothing else happens here -
 * the GL is in OpenGLDriver, on the side that owns the context. */
#define DECL_DRIVER_API(name, paramsDecl, params) \
	void Rendering::Context::Driver::name paramsDecl \
	{ \
		m_commandStream->name(MOON_EXPAND_ARGS params); \
	}

/* The same for a call that hands back a value at once (Filament's DECL_DRIVER_API_RETURN, e.g.
 * createBufferObject / createTexture): the handle is reserved here - the allocator is thread
 * safe, so the recording side can do it without the object existing - and the command that
 * constructs the object in that slot is recorded right behind it. Whatever the caller records
 * next resolves the handle, because the ring runs in order. */
#define DECL_DRIVER_API_RETURN(RetType, name, paramsDecl, params) \
	RetType Rendering::Context::Driver::name paramsDecl \
	{ \
		const RetType handle = Rendering::Backend::GetHandleAllocator() \
			.Allocate<Rendering::Backend::HandleRecord<RetType>>(); \
		m_commandStream->name(handle, MOON_EXPAND_ARGS params); \
		return handle; \
	}

#include <Rendering/Backend/DriverAPI.inc>
#undef DECL_DRIVER_COMMAND
#undef DECL_DRIVER_API
#undef DECL_DRIVER_API_RETURN
#undef MOON_EXPAND_ARGS

// ---- The synchronous halves of the driver API ------------------------------------------------
//
// These return the GPU's answer, so they cannot be a command: the query is recorded (it has to run
// where the context is) and waited for. Filament declares them DECL_DRIVER_API_RETURN too
// (getFenceStatus / mapBuffer) and its command stream serves them through getResult(); ours has no
// result channel, so the wait is FlushAndWait - which is why they are between-frame calls only.

bool Rendering::Context::Driver::IsBufferFenceSignaled(BufferHandle p_buffer)
{
	if (!static_cast<bool>(p_buffer))
	{
		return false;
	}

	if (IsExecutorThread())
	{
		// Already where the context is: ask directly instead of recording and waiting.
		const Rendering::HAL::Buffer* const buffer = ResolveBuffer(p_buffer);
		return buffer != nullptr && buffer->IsFenceSignaled();
	}

	bool signaled = false;
	Enqueue([&signaled, p_buffer]
		{
			const Rendering::HAL::Buffer* const buffer = ResolveBuffer(p_buffer);
			signaled = buffer != nullptr && buffer->IsFenceSignaled();
		});
	FlushAndWait();
	return signaled;
}

void* Rendering::Context::Driver::MapBufferRead(BufferHandle p_buffer, uint64_t p_offset, uint64_t p_size)
{
	if (!static_cast<bool>(p_buffer))
	{
		return nullptr;
	}

	if (IsExecutorThread())
	{
		Rendering::HAL::Buffer* const buffer = ResolveBuffer(p_buffer);
		return buffer != nullptr ? buffer->MapRead(p_offset, p_size) : nullptr;
	}

	// Filament's mapBuffer: the map runs where the context is and the pointer comes back. It stays
	// mapped until UnmapBufferObject runs, so the caller reads it while the range is mapped.
	void* mapped = nullptr;
	Enqueue([&mapped, p_buffer, p_offset, p_size]
		{
			Rendering::HAL::Buffer* const buffer = ResolveBuffer(p_buffer);
			mapped = buffer != nullptr ? buffer->MapRead(p_offset, p_size) : nullptr;
		});
	FlushAndWait();
	return mapped;
}

void Rendering::Context::Driver::Enqueue(std::function<void()> p_command)
{
	m_commandStream->Enqueue<::Rendering::Backend::CustomCommand>(std::move(p_command));
}

void Rendering::Context::Driver::BindDescriptor(const ::Rendering::Backend::BindDescriptorRequest& p_request)
{
	m_commandStream->BindDescriptor(p_request);
}

void Rendering::Context::Driver::ResizeFramebuffer(Data::Framebuffer& p_target, uint16_t p_width,
	uint16_t p_height)
{
	// Front-end bookkeeping: a resize is each attachment re-specifying its storage, and those are
	// recorded commands of their own (Data::Texture::Resize / Data::Renderbuffer::Resize). Running it
	// here, on the recording side, is what keeps the attachment list the passes read back current.
	p_target.Resize(p_width, p_height);
}

void Rendering::Context::Driver::CopyFramebufferColor(
	Data::Framebuffer& p_source, uint32_t p_sourceIndex,
	Data::Framebuffer& p_target, uint32_t p_targetIndex)
{
	// Filament's blit: the two targets travel as handles, and so do their sizes - the attachment list
	// that says how big they are is the front end's (a resize updates it, and this may run before the
	// executor has seen that command).
	const auto [sourceWidth, sourceHeight] = p_source.GetSize();
	const auto [targetWidth, targetHeight] = p_target.GetSize();
	CopyFramebufferColorObject(p_source.GetHandle(), p_sourceIndex, sourceWidth, sourceHeight,
		p_target.GetHandle(), p_targetIndex, targetWidth, targetHeight);
}

void Rendering::Context::Driver::CopyFramebufferDepth(
	Data::Framebuffer& p_source, Data::Framebuffer& p_target)
{
	const auto [sourceWidth, sourceHeight] = p_source.GetSize();
	const auto [targetWidth, targetHeight] = p_target.GetSize();
	CopyFramebufferDepthObject(p_source.GetHandle(), sourceWidth, sourceHeight,
		p_target.GetHandle(), targetWidth, targetHeight);
}

void Rendering::Context::Driver::BindFramebuffer(const Data::Framebuffer* p_target)
{
	// Filament's bindFramebuffer: the target is named by its handle, and a null one is the default
	// framebuffer (the front end's own pointer never crosses to the other side).
	BindFramebufferObject(p_target != nullptr
		? p_target->GetHandle()
		: ::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwFramebuffer>{});
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

namespace
{
	/** The state a synchronous driver call waits on: the barrier command signals it from the thread
	 * that runs the batch, and the caller sleeps until it does. */
	struct CommandBarrier
	{
		std::mutex lock;
		std::condition_variable condition;
		bool done = false;
	};
}

bool Rendering::Context::Driver::IsExecutorThread() const
{
	return m_renderThreadActive && std::this_thread::get_id() == m_renderThreadId;
}

void Rendering::Context::Driver::FlushAndWait()
{
	if (IsExecutorThread())
	{
		// Already where the commands run: recording from here would land after the batch being
		// executed, so the caller has to do its work inline.
		return;
	}

	if (!m_renderThreadActive)
	{
		// Single-threaded: the batch runs on this thread, which is what Execute does at the end of
		// the frame. The synchronous calls only happen between frames (a pick, a screenshot, a fence
		// query), so executing here is the same point in time.
		Flush();
		Execute();
		return;
	}

	// With a render thread: a barrier command goes after the recorded work, then the batch is
	// published and this thread sleeps until the barrier has run.
	auto barrier = std::make_shared<CommandBarrier>();
	Enqueue([barrier]
		{
			{
				std::lock_guard lock(barrier->lock);
				barrier->done = true;
			}
			barrier->condition.notify_all();
		});
	Flush();

	std::unique_lock lock(barrier->lock);
	barrier->condition.wait(lock, [&barrier] { return barrier->done; });
}

void Rendering::Context::Driver::ExecuteAll()
{	// Resources whose last reference was dropped asynchronously (a mesh freed by a feature rebuild
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
	// The executor activates the module's context for the batch it is about to run: a batch can
	// predate the frame's own MakeCurrent command (everything recorded while the module and its
	// views were built), and in the single-threaded configuration these run on a thread whose
	// context Qt controls. On the driver thread this is a no-op.
	if (m_openGLDriver != nullptr)
	{
		m_openGLDriver->MakeCurrent();
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
	Rendering::Backend::DeferredDeleter::Flush();
	const std::vector<Rendering::Backend::CircularBuffer::Range> batches = m_commandQueue->WaitForCommands();
	if (batches.empty())
	{
		return false;
	}
	// Same contract as ExecuteAll(): the context is made current here, on the side that runs the
	// batch, so a batch recorded before any MakeCurrent command still has one.
	if (m_openGLDriver != nullptr)
	{
		m_openGLDriver->MakeCurrent();
	}
	for (const Rendering::Backend::CircularBuffer::Range& batch : batches)
	{
		m_openGLDriver->Execute(batch.tail, batch.head);
		m_commandQueue->ReleaseBuffer(batch);
	}
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
	m_commandStream->MakeCurrent();
}

void Rendering::Context::Driver::RequestExit()
{
	m_commandQueue->RequestExit();
}

bool Rendering::Context::Driver::Execute()
{
	Rendering::Backend::DeferredDeleter::Flush();
	const std::vector<Rendering::Backend::CircularBuffer::Range> batches = m_commandQueue->WaitForCommands();
	if (batches.empty())
	{
		return false;
	}
	// Same contract as ExecuteAll(): the context is made current here, on the side that runs the
	// batch, so a batch recorded before any MakeCurrent command still has one.
	if (m_openGLDriver != nullptr)
	{
		m_openGLDriver->MakeCurrent();
	}
	for (const Rendering::Backend::CircularBuffer::Range& batch : batches)
	{
		m_openGLDriver->Execute(batch.tail, batch.head);
		m_commandQueue->ReleaseBuffer(batch);
	}
	return true;
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
