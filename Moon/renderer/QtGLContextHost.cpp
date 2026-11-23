#include "QtGLContextHost.h"

#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <Rendering/HAL/Common/HALDriverAccess.h>
#include <thread>

#include <Rendering/Context/Driver.h>
#include <Rendering/HAL/Backend.h>
#include <QSurfaceFormat>

#include "core/log.h"

Editor::Core::QtGLContextHost::QtGLContextHost() = default;

Editor::Core::QtGLContextHost::QtGLContextHost(QOpenGLContext* p_shareContext)
{
	if (p_shareContext == nullptr)
	{
		CORE_ERROR("[GL] no viewport context to share with: the render module has no context");
		return;
	}

	m_context = std::make_unique<QOpenGLContext>();
	m_context->setFormat(p_shareContext->format());
	m_context->setShareContext(p_shareContext);
	if (!m_context->create())
	{
		CORE_ERROR("[GL] could not create the render module's context (sharing with the viewport)");
		m_context.reset();
		return;
	}

	// The context has to be current against a surface to issue any GL call; an offscreen one
	// keeps the module independent of the widget's window.
	m_surface = std::make_unique<QOffscreenSurface>();
	m_surface->setFormat(m_context->format());
	m_surface->create();
	if (!m_surface->isValid())
	{
		CORE_ERROR("[GL] could not create the render module's offscreen surface");
		m_surface.reset();
		m_context.reset();
		return;
	}

	CORE_INFO("[GL] the render module now has its own context, sharing objects with the editor's windows");
}

Editor::Core::QtGLContextHost::~QtGLContextHost()
{
	if (m_context != nullptr)
	{
		DoneCurrent();
		if (m_context->surface() != nullptr)
		{
			m_context->makeCurrent(m_surface.get());
			m_context->doneCurrent();
		}
	}
	m_surface.reset();
	m_context.reset();
}

bool Editor::Core::QtGLContextHost::IsValid() const
{
	return m_context != nullptr && m_surface != nullptr;
}

QOpenGLContext* Editor::Core::QtGLContextHost::Context() const
{
	return m_context.get();
}

bool Editor::Core::QtGLContextHost::MakeCurrent()
{
	if (!IsValid())
	{
		return false;
	}

	// Filament's platform invariant (see PlatformGlfwGL::makeCurrent): the module's context is made
	// current on the driver's thread, and nowhere else. Another thread taking it here would run its
	// GL against a context the executor is using, which is a hard crash inside the driver - not a
	// GL error. When the module has a render thread, everyone else gets false and skips its GL.
	if (::Rendering::Context::Driver* driver = ::Rendering::HAL::CurrentDriver();
		driver != nullptr && driver->IsRenderThreadActive()
		&& std::this_thread::get_id() != driver->GetRenderThreadId())
	{
		return false;
	}

	// Already current means there is nothing to do: Qt's makeCurrent on an already-current context
	// is a no-op, but keeping it out of the way makes the ownership explicit.
	if (QOpenGLContext::currentContext() == m_context.get())
	{
		return true;
	}
	return m_context->makeCurrent(m_surface.get());
}

void Editor::Core::QtGLContextHost::DoneCurrent()
{
	if (m_context != nullptr)
	{
		m_context->doneCurrent();
	}
}

bool Editor::Core::QtGLContextHost::IsCurrent() const
{
	return m_context != nullptr && QOpenGLContext::currentContext() == m_context.get();
}

bool Editor::Core::QtGLContextHost::PrepareSurface(QOpenGLContext* p_shareContext)
{
	// Qt only allows a QOffscreenSurface to be created on the GUI thread. The render thread creates
	// the context (that it may do), so the surface it will be made current on is prepared here,
	// before the engine starts: a surface created on the render thread is invalid, makeCurrent fails,
	// and every GL call after that blocks.
	if (m_surface != nullptr && m_surface->isValid())
	{
		return true;
	}

	m_surface = std::make_unique<QOffscreenSurface>();
	// The format has to be the one the context will have, or makeCurrent refuses the pair.
	if (p_shareContext != nullptr)
	{
		QSurfaceFormat format = p_shareContext->format();
		format.setSamples(0);
		m_surface->setFormat(format);
	}
	m_surface->create();
	if (!m_surface->isValid())
	{
		CORE_ERROR("[GL] could not create the module's offscreen surface");
		m_surface.reset();
		return false;
	}
	return m_surface->isValid();
}

::Rendering::Context::Driver* Editor::Core::QtGLContextHost::CreateDriver(void* p_sharedContext)
{
	// Mirrors Qt-Filament's PlatformGlfwGL::createDriver, which is the shape that works: the context
	// and the offscreen surface are both created here - on the thread that will make them current -
	// with the SAME format, samples forced to 0. A surface whose format does not match the context
	// (an MSAA widget format against a plain offscreen surface, say) still reports create() ok and
	// makeCurrent() ok, and then every GL call is a no-op that blocks: that is the trap.
	if (!IsValid())
	{
		auto* shared = static_cast<QOpenGLContext*>(p_sharedContext);
		QSurfaceFormat format = shared != nullptr ? shared->format() : QSurfaceFormat::defaultFormat();
		format.setSamples(0);

		m_context = std::make_unique<QOpenGLContext>();
		m_context->setFormat(format);
		if (shared != nullptr)
		{
			m_context->setShareContext(shared);
		}
		if (!m_context->create())
		{
			CORE_ERROR("[GL] could not create the render module's context");
			m_context.reset();
			return nullptr;
		}

		// The surface is created on the GUI thread (PrepareSurface): Qt does not support creating a
		// QOffscreenSurface on this one, and a surface created here is invalid - makeCurrent reports
		// success while nothing can be drawn to it. Only make one when the host was not given any.
		if (m_surface == nullptr || !m_surface->isValid())
		{
			m_surface = std::make_unique<QOffscreenSurface>();
			m_surface->setFormat(format);
			m_surface->create();
		}
		if (m_surface == nullptr || !m_surface->isValid())
		{
			CORE_ERROR("[GL] could not create the render module's offscreen surface");
			m_surface.reset();
			m_context.reset();
			return nullptr;
		}
		CORE_INFO("[GL] the render module now has its own context, sharing objects with the editor's windows");
	}

	if (!MakeCurrent())
	{
		CORE_ERROR("[GL] could not make the render module's context current on its thread");
		return nullptr;
	}



	auto driver = std::make_unique<::Rendering::Context::Driver>(::Rendering::Settings::DriverSettings{ true });
	// The context stays current, like Filament's Platform::createDriver: the engine builds its
	// Context - and whatever the host callback creates on that context (ImGui's font texture, the
	// gizmo geometry) - on this thread right after this returns, and all of it needs the context
	// current. The caller closes the context when the build is done.
	driver->SetContextHost(this);
	return driver.release();
}

void Editor::Core::QtGLContextHost::DestroyDriver(::Rendering::Context::Driver* p_driver)
{
	// The driver owns GL objects, so it goes with the module's context current, on the same thread
	// that created it.
	const bool bound = MakeCurrent();
	delete p_driver;
	if (bound)
	{
		DoneCurrent();
	}
}
