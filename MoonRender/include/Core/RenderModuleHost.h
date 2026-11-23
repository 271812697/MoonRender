#pragma once
#include <functional>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <memory>

namespace Core
{
	class Context;
}

namespace Rendering::Context
{
	class IGLContextHost;
	class IPlatform;
	class Driver;
}

namespace Core
{
	/** The render module: the engine (Core::Context: driver, managers, scene manager) plus the
	 * GL context it lives on.
	 *
	 * There is exactly one of these in a process, and everything that renders goes through it:
	 * the scene view, the material preview, every overlay. A second module would be a second set
	 * of resource managers over the same assets, which the editor cannot be in - so it is a
	 * singleton, and a window that shows anything asks for it rather than owning one.
	 *
	 * **The context comes from outside.** This class is engine-side and stays free of the window
	 * system: whoever has a GL context (the editor, through its Qt host) implements
	 * Rendering::Context::IGLContextHost and hands one in. That is the same split Filament uses
	 * with its Platform: the engine asks for a context, the host provides one, and the engine
	 * never learns whether it came from Qt, GLFW or a headless driver.
	 *
	 * It does not own views. A view owns render targets, and those belong to whoever draws with
	 * them (the viewport owns its SceneView): the host is what a view is created *on* - make the
	 * context current, create the view, close it again.
	 *
	 * Framebuffers and vertex arrays belong to the context that created them, so the whole module
	 * is built with that context current, in one call, before anything else runs. Whatever else
	 * has to be created there (the editor's ImGui/Im3D overlay state, say) is supplied as the
	 * build callback, which runs inside that call.
	 */
	class RenderModuleHost final
	{
	public:
		/** The module, created on p_contextHost when there is none yet. Takes ownership of the
		 * context; a null host means "no context of my own", and the caller then builds the
		 * module on whatever context it has current (BuildOnCurrentContext).
		 *
		 * The build callback runs at the end of the build, with the context current; it is where
		 * the host-side GL objects that belong to the module's context are created.
		 *
		 * Returns the module; IsReady() says whether it could be built. */
		static RenderModuleHost& Create(
			std::unique_ptr<::Rendering::Context::IGLContextHost> p_contextHost,
			std::function<void()> p_buildCallback = {});

		/** Filament's shape: the platform creates the driver - and with it the GL context - on the
		 * calling thread, and the engine adopts it. This is the entry point the render thread
		 * will use; while the frame still records and executes on the caller's thread it is
		 * called from there, which keeps today's behaviour identical. */
		static RenderModuleHost& Create(
			std::unique_ptr<::Rendering::Context::IPlatform> p_platform,
			void* p_sharedContext,
			std::function<void()> p_buildCallback = {});

		/** The module, or null before a viewport has created it. Nothing renders before that,
		 * and asking is legal: a tool that may be reached during start-up asks. */
		static RenderModuleHost* Instance();

		/** Takes the module down - the viewport that created it is shutting down. Views are not
		 * part of this: whoever owns one has taken it down already. Calling this with no module
		 * is not an error. */
		static void Destroy();

		/** Builds the engine on whatever context is current - what a host without a context of
		 * its own needs. Idempotent. */
		void BuildOnCurrentContext();

		/** True when the batches - and with them the context - belong to the engine's own thread.
		 * Callers that would otherwise run GL on the calling thread ask this and hand the work to
		 * the driver instead. */
		bool IsThreaded() const { return m_threaded; }

		/** Whether the engine runs the batches on a thread of its own: the default configuration
		 * (Filament's), with MOON_RENDER_THREAD=0 as the single-threaded opt-out. Asked before the
		 * module exists too - the window picks its share context from it. */
		static bool ThreadedMode();

		/** True when the module has a context of its own, false when it renders on the caller's. */
		bool HasOwnContext() const;
		/** True once the Context exists: from then on the engine's services are provided and
		 * views can be created on the module's context. */
		bool IsReady() const;

		/** Makes the module's context current, when it has one. False means the module is not
		 * built and nothing can be rendered. */
		bool MakeCurrent();
		void DoneCurrent();

		/** Filament's Engine::execute - this host is the engine, so the loop calls it here.
		 *
		 * The frame is recorded by the view, which only flushes (publishes) it; this is where it
		 * runs. Without a render thread it runs on the calling thread (FEngine::execute on the
		 * recording thread); with one, the batch belongs to the driver thread and this publishes it
		 * and waits until it has run, because the caller is the frame's consumer - it pastes the
		 * result. Once per frame, after the recording. */
		void Execute();

		/** The driver, the managers and the scene manager. Null before the module is built. */
		Context* GetContext() const;

	private:
		explicit RenderModuleHost(std::unique_ptr<::Rendering::Context::IGLContextHost> p_contextHost,
			std::function<void()> p_buildCallback);
		RenderModuleHost(std::unique_ptr<::Rendering::Context::IPlatform> p_platform,
			void* p_sharedContext, std::function<void()> p_buildCallback);
		~RenderModuleHost();

		RenderModuleHost(const RenderModuleHost&) = delete;
		RenderModuleHost& operator=(const RenderModuleHost&) = delete;

		void Build();

		/** Adopts a driver the platform created - the Context and whatever the host needs on that
		 * context. */
		void BuildOnDriver(std::unique_ptr<::Rendering::Context::Driver> p_driver);

		/** The render thread's body (Filament's FEngine::loop): create the driver - and with it the
		 * context - on this thread, build, then run batches until the queue asks to exit. */
		void Loop();

		/** The module is created and destroyed through the static calls above, so its own
		 * destructor is private - and what holds it has to be able to call it. */
		struct Destroyer
		{
			void operator()(RenderModuleHost* p_module) const { delete p_module; }
		};

		std::unique_ptr<::Rendering::Context::IGLContextHost> m_contextHost;
		/** Set when the driver came from the platform, so teardown goes back through it. */
		std::unique_ptr<::Rendering::Context::IPlatform> m_platform;
		std::function<void()> m_buildCallback;
		Context* m_context = nullptr;
		/** The driver the platform returned, kept so DestroyDriver can see it again. */
		::Rendering::Context::Driver* m_platformDriver = nullptr;
		/** The platform, when it is also the thing that binds the context (it is, for Qt): the
		 * engine binds per frame exactly as it does for a context host of its own. */
		::Rendering::Context::IGLContextHost* m_platformContextHost = nullptr;

		/** Set when the batches run on m_driverThread. */
		bool m_threaded = false;
		void* m_sharedContext = nullptr;
		std::thread m_driverThread;
		std::mutex m_readyLock;
		std::condition_variable m_readyCondition;
		bool m_ready = false;

		static std::unique_ptr<RenderModuleHost, Destroyer> s_instance;
	};
}
