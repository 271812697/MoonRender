#pragma once
#include <memory>

class QOpenGLContext;

namespace Editor::Core
{
	class QtGLContextHost;
	class Context;
}

namespace Editor::Core
{
	/** The render module: the GL context the editor's scene is drawn on, and the editor
	 * Context that lives on it (driver, resource managers, scene manager).
	 *
	 * There is exactly one of these in a process, and everything that renders goes through
	 * it: the scene view, the material preview, every overlay. A second module would be a
	 * second set of resource managers over the same assets, which is not something the
	 * editor can be in - so it is a singleton, and a window that shows anything asks for it
	 * rather than owning one.
	 *
	 * It does not own views. A view owns render targets, and those belong to whoever draws
	 * with them (the viewport owns its SceneView, see ViewportFrame): the host is what a
	 * view is *created on* - make it current, create the view, close it again - and what
	 * keeps the Context every view draws through alive.
	 *
	 * Framebuffers and vertex arrays belong to the context that created them, so the whole
	 * module is built with that context current, in one call, before anything else runs: a
	 * service that is missing when a panel or a timer asks for it throws, and in a release
	 * build that is an uncaught exception that takes the process down without a word.
	 *
	 * The module gets a context of its own, in the same share group as the editor's windows:
	 * the module renders there, the widgets only paste the result. Sharing needs a context
	 * that is *not* current (wglShareLists refuses that with ERROR_BUSY), which is why the
	 * viewport releases its own before calling Create.
	 */
	class RenderModuleHost final
	{
	public:
		/** The module, created on p_shareContext's share group when there is none yet.
		 *
		 * p_shareContext is the context whose group the module joins: the viewport's, or
		 * Qt's global share context when the application has one. It must not be current
		 * when this is called.
		 *
		 * When the context cannot be created (sharing refused, or it could not be made
		 * current) the module is left unbuilt: HasOwnContext() is false, and the caller
		 * makes its own context current and calls BuildOnCurrentContext, which is how the
		 * module ran before the split. */
		static RenderModuleHost& Create(QOpenGLContext* p_shareContext);

		/** The module, or null before a viewport has created it. Nothing renders before
		 * that, and asking is legal: a tool that may be reached during start-up asks. */
		static RenderModuleHost* Instance();

		/** Takes the module down - the viewport that created it is shutting down.
		 *
		 * Views are not part of this: whoever owns one has taken it down already, with the
		 * module's context current (see ViewportFrame). Calling this with no module is not
		 * an error. */
		static void Destroy();

		/** Builds the module on whatever context is current - what a host without a context
		 * of its own needs. Idempotent. */
		void BuildOnCurrentContext();

		/** True when the module renders on a context of its own, false when it renders on
		 * the window's. */
		bool HasOwnContext() const;
		/** True once the Context exists: from then on the editor's services are provided
		 * and views can be created on the module's context. */
		bool IsReady() const;

		/** Makes the module's context current, when it has one. False means the module is
		 * not built and nothing can be rendered. */
		bool MakeCurrent();
		void DoneCurrent();

		/** The driver, the managers and the scene manager. Null before the module is built. */
		Context* GetContext() const;

	private:
		explicit RenderModuleHost(QOpenGLContext* p_shareContext);
		~RenderModuleHost();

		RenderModuleHost(const RenderModuleHost&) = delete;
		RenderModuleHost& operator=(const RenderModuleHost&) = delete;

		void Build();

		/** The module is created and destroyed through the static calls above, so its own
		 * destructor is private - and what holds it has to be able to call it. */
		struct Destroyer
		{
			void operator()(RenderModuleHost* p_module) const { delete p_module; }
		};

		std::unique_ptr<QtGLContextHost> m_contextHost;
		Context* m_context = nullptr;

		static std::unique_ptr<RenderModuleHost, Destroyer> s_instance;
	};
}
