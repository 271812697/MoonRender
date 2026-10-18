#include "QtGLContextHost.h"

#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QSurfaceFormat>

#include "core/log.h"

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

	CORE_INFO("[GL] the render module now has its own context, sharing objects with the viewport");
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

bool Editor::Core::QtGLContextHost::MakeCurrent()
{
	if (!IsValid())
	{
		return false;
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
