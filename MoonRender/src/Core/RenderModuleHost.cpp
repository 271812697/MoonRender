#include <Core/RenderModuleHost.h>

#include <cstdio>
#include <string_view>
#include <glad/glad.h>
#include <cstdlib>

#include <Core/Context.h>
#include <Rendering/Context/IGLContextHost.h>
#include <Rendering/Context/IPlatform.h>
#include <Rendering/Context/Driver.h>

namespace Core
{
	std::unique_ptr<RenderModuleHost, RenderModuleHost::Destroyer> RenderModuleHost::s_instance;

	RenderModuleHost& RenderModuleHost::Create(
		std::unique_ptr<::Rendering::Context::IGLContextHost> p_contextHost,
		std::function<void()> p_buildCallback)
	{
		if (!s_instance)
		{
			s_instance.reset(new RenderModuleHost(std::move(p_contextHost), std::move(p_buildCallback)));
		}
		return *s_instance;
	}

	RenderModuleHost* RenderModuleHost::Instance()
	{
		return s_instance.get();
	}

	void RenderModuleHost::Destroy()
	{
		// Everything the module owns was created on its context, and the destructor makes that
		// context current before it lets go of any of it.
		s_instance.reset();
	}

	RenderModuleHost::RenderModuleHost(
		std::unique_ptr<::Rendering::Context::IGLContextHost> p_contextHost,
		std::function<void()> p_buildCallback)
		: m_contextHost(std::move(p_contextHost))
		, m_buildCallback(std::move(p_buildCallback))
	{
		if (m_contextHost == nullptr)
		{
			// No context of its own: the caller builds the module on whatever it has current
			// (BuildOnCurrentContext), which is how the editor worked before the split.
			return;
		}

		if (m_contextHost->MakeCurrent())
		{
			Build();
			m_contextHost->DoneCurrent();
		}
		else
		{
			// Sharing refused, or the context could not be made current.
			m_contextHost.reset();
		}
	}

	RenderModuleHost::~RenderModuleHost()
	{
		if (m_threaded)
		{
			// The batches - and the Context with them - belong to the render thread: ask its queue
			// to stop and wait for the loop to finish tearing the Context down there.
			if (m_context != nullptr && m_context->driver != nullptr)
			{
				m_context->driver->RequestExit();
			}
			if (m_driverThread.joinable())
			{
				m_driverThread.join();
			}
			return;
		}

		// The Context's managers hold GL objects, so it has to be destroyed with the module's
		// context current: a texture or a buffer deleted from another context either does nothing
		// or deletes a name that belongs to someone else. Views are the responsibility of whoever
		// created them: they are gone by the time this runs.
		const bool onOwnContext = m_contextHost != nullptr && m_contextHost->MakeCurrent();
		delete m_context;
		m_context = nullptr;
		if (onOwnContext)
		{
			m_contextHost->DoneCurrent();
		}
	}

	void RenderModuleHost::BuildOnCurrentContext()
	{
		Build();
	}

	bool RenderModuleHost::HasOwnContext() const
	{
		return m_contextHost != nullptr;
	}

	bool RenderModuleHost::IsReady() const
	{
		return m_context != nullptr;
	}

	bool RenderModuleHost::MakeCurrent()
	{
		if (!IsReady())
		{
			return false;
		}

		// On the threaded path the context belongs to the render thread and never becomes current
		// here: a QOpenGLContext cannot be made current in another thread at all (Qt aborts the
		// process). The caller's work is recording, which needs no context.
		if (m_threaded)
		{
			return true;
		}

		// Without a context of its own the module renders on the caller's, which has it current
		// already when it draws.
		if (m_contextHost != nullptr)
		{
			return m_contextHost->MakeCurrent();
		}
		return m_platformContextHost != nullptr ? m_platformContextHost->MakeCurrent() : true;
	}

	void RenderModuleHost::DoneCurrent()
	{
		if (m_threaded)
		{
			return;
		}

		if (m_contextHost != nullptr)
		{
			m_contextHost->DoneCurrent();
		}
	}
	void RenderModuleHost::Flush() {
		if (m_context == nullptr || m_context->driver == nullptr)
		{
			return;
		}

		m_context->driver->Flush();
	}
	void RenderModuleHost::Execute()
	{
		// Filament's Engine::execute. The frame's recording was published by the view (it flushes at
		// the end of its Render), so this is the one point that runs it: on this thread when the
		// module has no render thread, on the driver thread otherwise - where the wait below is what
		// makes the frame readable for the paste that comes next.
		if (m_context == nullptr || m_context->driver == nullptr)
		{
			return;
		}

		m_context->driver->Execute();
	}

	Context* RenderModuleHost::GetContext() const
	{
		return m_context;
	}

	void RenderModuleHost::Build()
	{
		if (m_context != nullptr)
		{
			return;
		}

		m_context = new Context("", "");
		m_context->sceneManager.LoadDefaultScene();

		if (m_contextHost != nullptr && m_context->driver != nullptr)
		{
			m_context->driver->SetContextHost(m_contextHost.get());
			// The global GL state the backend sets up is per context, and the module's context is
			// new: reversed-Z depth range, blend func, seamless cubemap, debug output.
			m_context->driver->ReapplyGlobalState();
		}

		// Whatever the host has to create on this context belongs here, before anything draws.
		if (m_buildCallback)
		{
			m_buildCallback();
		}

		if (m_context->driver != nullptr)
		{
			// Queued resource work (resource destruction today; uploads when the render side owns
			// them) has to run before anything draws: the overlays and the gizmos draw outside the
			// command path. Draining runs it with the context current.
			//m_context->driver->ExecutePendingCommands();
		}
	}
}

Core::RenderModuleHost& Core::RenderModuleHost::Create(
	std::unique_ptr<::Rendering::Context::IPlatform> p_platform,
	void* p_sharedContext,
	std::function<void()> p_buildCallback)
{
	if (!s_instance)
	{
		s_instance.reset(new RenderModuleHost(std::move(p_platform), p_sharedContext, std::move(p_buildCallback)));
	}
	return *s_instance;
}

Core::RenderModuleHost::RenderModuleHost(
	std::unique_ptr<::Rendering::Context::IPlatform> p_platform,
	void* p_sharedContext,
	std::function<void()> p_buildCallback)
	: m_platform(std::move(p_platform))
	, m_buildCallback(std::move(p_buildCallback))
	, m_sharedContext(p_sharedContext)
{
	if (m_platform == nullptr)
	{
		return;
	}

	m_platformContextHost = dynamic_cast<::Rendering::Context::IGLContextHost*>(m_platform.get());

	if (ThreadedMode())
	{
		// Filament's shape: the thread comes first, and it creates the driver (and with it the
		// context) inside its own loop. The caller waits until the module is ready, because
		// everything that asks for it next - views, materials, the overlay - assumes it exists.
		m_threaded = true;
		m_driverThread = std::thread([this] { Loop(); });

		std::unique_lock lock(m_readyLock);
		m_readyCondition.wait(lock, [this] { return m_ready; });
		return;
	}

	// Single-threaded: the platform creates the driver on this thread - the same call the loop
	// makes - and the engine adopts it.
	m_platformDriver = m_platform->CreateDriver(p_sharedContext);
	if (m_platformDriver == nullptr)
	{
		return;
	}

	BuildOnDriver(std::unique_ptr<::Rendering::Context::Driver>(m_platformDriver));
}

bool Core::RenderModuleHost::ThreadedMode()
{
	// On by default, Filament's configuration: the batches run on the engine's own thread, and the
	// recording side only writes commands. MOON_RENDER_THREAD=0 is the single-threaded build-up
	// (the app calls Execute() itself, i.e. Engine::execute on the recording thread) - it is what
	// the older pixel baselines were captured with, so it stays reachable.
	static const bool threaded = [] {
		const char* const value = std::getenv("MOON_RENDER_THREAD");
		return value == nullptr || std::string_view(value) != "0";
	}();
	return threaded;
}

void Core::RenderModuleHost::Loop()
{
	// The driver - and the GL context in it - is created here, on this thread, exactly as
	// Filament's Platform::createDriver is called from FEngine::loop.
	m_platformDriver = m_platform->CreateDriver(m_sharedContext);
	if (m_platformDriver == nullptr)
	{
		std::lock_guard lock(m_readyLock);
		m_ready = true;
		m_readyCondition.notify_all();
		return;
	}

	BuildOnDriver(std::unique_ptr<::Rendering::Context::Driver>(m_platformDriver));

	if (m_context != nullptr && m_context->driver != nullptr)
	{
		m_context->driver->SetRenderThreadActive(true);
	}

	{
		std::lock_guard lock(m_readyLock);
		m_ready = true;
		m_readyCondition.notify_all();
	}

	if (m_context == nullptr || m_context->driver == nullptr)
	{
		return;
	}

	// The loop keeps the context current: the batches it runs are GL.
	if (m_platformContextHost != nullptr && !m_platformContextHost->MakeCurrent())
	{
		// Without it every GL call on this thread blocks: say so instead of hanging.
		std::fprintf(stderr, "[GL] the render thread could not make the module's context current\n");
		std::fflush(stderr);
	}

	// Sanity check now that the context should be current on this thread: if it is not, every GL
	// call after this point blocks, so say so instead of hanging.
	{
		const GLubyte* vendor = glGetString(GL_VENDOR);
		GLint viewport[4] = { 0, 0, 0, 0 };
		glGetIntegerv(GL_VIEWPORT, viewport);
		std::fprintf(stderr, "[GL] render thread: current=%d vendor=%s viewport=%d,%d %dx%d\n",
			(m_platformContextHost != nullptr && m_platformContextHost->IsCurrent()) ? 1 : 0,
			vendor != nullptr ? reinterpret_cast<const char*>(vendor) : "(null)",
			viewport[0], viewport[1], viewport[2], viewport[3]);
		std::fflush(stderr);
	}

	while (true)
	{
		m_context->driver->Execute();
	}

	// Teardown lands on this thread too: the Context owns the driver, and the driver owns GL
	// objects, so it has to be destroyed with the context current.
	if (m_platformContextHost != nullptr)
	{
		m_platformContextHost->MakeCurrent();
	}
	delete m_context;
	m_context = nullptr;
	m_platformDriver = nullptr;
	if (m_platformContextHost != nullptr)
	{
		m_platformContextHost->DoneCurrent();
	}
}

void Core::RenderModuleHost::BuildOnDriver(std::unique_ptr<::Rendering::Context::Driver> p_driver)
{
	m_context = new Context(std::move(p_driver));
	m_context->sceneManager.LoadDefaultScene();
	if (m_context->driver != nullptr)
	{
		// The context the platform created is new: the global GL state the backend sets up
		// (reversed-Z depth range, blend func, seamless cubemap) belongs to it.
		m_context->driver->ReapplyGlobalState();
	}

	if (m_buildCallback)
	{
		m_buildCallback();
	}
	// The build ran with the context the platform created current; the frame binds it per use.
	if (m_platformContextHost != nullptr)
	{
		m_platformContextHost->DoneCurrent();
	}
}
