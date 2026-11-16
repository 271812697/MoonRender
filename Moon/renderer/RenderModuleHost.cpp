#include "RenderModuleHost.h"

#include "QtGLContextHost.h"
#include "Context.h"

#include "Interactive/Im3DRenderer.h"
#include "core/log.h"

#include <QOpenGLContext>

namespace Editor::Core
{
	std::unique_ptr<RenderModuleHost, RenderModuleHost::Destroyer> RenderModuleHost::s_instance;

	RenderModuleHost& RenderModuleHost::Create(QOpenGLContext* p_shareContext)
	{
		if (!s_instance)
		{
			s_instance.reset(new RenderModuleHost(p_shareContext));
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

	RenderModuleHost::RenderModuleHost(QOpenGLContext* p_shareContext)
	{
		if (p_shareContext == nullptr)
		{
			return;
		}

		m_contextHost = std::make_unique<QtGLContextHost>(p_shareContext);
		if (m_contextHost->IsValid() && m_contextHost->MakeCurrent())
		{
			Build();
			m_contextHost->DoneCurrent();
		}
		else
		{
			// No module context (sharing refused, or it could not be made current): the
			// caller builds the module on the window's context, which is how it worked
			// before the split. Nothing is lost visually - only the separation of the two
			// contexts is.
			CORE_WARN("[GL] the render module has no context of its own; it has to be built on the window's");
			m_contextHost.reset();
		}
	}

	RenderModuleHost::~RenderModuleHost()
	{
		// The Context's managers hold GL objects, so it has to be destroyed with the module's
		// context current: a texture or a buffer deleted from another context either does
		// nothing or deletes a name that belongs to someone else. Views are the responsibility
		// of whoever created them (see ViewportFrame): they are gone by the time this runs.
		const bool onOwnContext = m_contextHost != nullptr && m_contextHost->IsValid() && m_contextHost->MakeCurrent();
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
		// Without a context of its own the module renders on the window's, and the window
		// has it current already when it draws.
		return m_contextHost != nullptr ? m_contextHost->MakeCurrent() : true;
	}

	void RenderModuleHost::DoneCurrent()
	{
		if (m_contextHost != nullptr)
		{
			m_contextHost->DoneCurrent();
		}
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

		// On a file after another the Context would be replaced, but a second one here only
		// happens through BuildOnCurrentContext after a failed constructor: nothing was
		// built then.
		m_context = new Context("", "");
		m_context->sceneManager.LoadDefaultScene();

		// The overlays belong to the module's frame, so their GL objects belong to its
		// context too: the gizmo geometry (vertex array, vertex buffer, materials) and
		// ImGui's font texture and vertex array. Vertex arrays and textures are per context
		// - a name from the other one is not the object at all, which is how the ViewCube
		// and the rotation centre silently stopped drawing when only half of this was on the
		// right context.
		MOON::ImRenderer::instance().initGizmoGl();
		MOON::ImRenderer::instance().init();

		if (m_contextHost != nullptr && m_context->driver != nullptr)
		{
			m_context->driver->SetContextHost(m_contextHost.get());
			// The global GL state the backend sets up is per context, and the module's
			// context is new: reversed-Z depth range, blend func, seamless cubemap, debug
			// output.
			m_context->driver->ReapplyGlobalState();
		}
	}
}
