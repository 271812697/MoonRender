#include "viewerwidget.h"

#include "viewportframe.h"

#include "renderer/RenderModuleHost.h"
#include "glloader.h"
#include "core/callbackManager.h"
#include "Core/Global/ServiceLocator.h"

#include <QOpenGLContext>

namespace MOON {
	ViewerWidget::ViewerWidget(QWidget* p_parent) : QOpenGLWidget(p_parent)
	{
		this->setFocusPolicy(Qt::StrongFocus);
		this->setMouseTracking(true);
		QSurfaceFormat format;
		// Single sample: the frame is pasted into this widget's framebuffer with a blit, and
		// blitting into a multisampled framebuffer (even with one sample) is invalid. The
		// render module does its own MSAA on its side (AView::m_msaaframebuffer).
		format.setSamples(0);
		this->setFormat(format);
		// The widget itself is a service: the tools that draw with the module's context
		// (ImGui's input, the view parameters, the picture a project card is shown by) need
		// the surface they belong to. It is only ever reached for the widget - what is open
		// and what the scene shows are services of their own now.
		RegService(ViewerWidget, *this);
	}

	ViewerWidget::~ViewerWidget()
	{
		// In this order, with the right context current for each part: the paster's
		// framebuffer belongs to this widget's context, the scene view and the editor Context
		// belong to the module's (the frame makes that context current itself before taking
		// the view down).
		this->makeCurrent();
		m_paster.Release();
		m_frame.reset();
		this->doneCurrent();
		// Nothing may be left pointing at a widget that is gone.
		::Core::Global::ServiceLocator::Remove<ViewerWidget>();
		// The module goes last, and only now: this widget is the surface the module shares its
		// group with, so it is the one that creates it - and the one that takes it down, once
		// the frame that drew on it is gone.
		Editor::Core::RenderModuleHost::Destroy();
	}

	void ViewerWidget::initializeGL()
	{
		QOpenGLWidget::initializeGL();
		// The editor's own GL entry points, resolved from this widget's context - the first
		// one the editor has.
		InstallGlLoader(context());
		this->startTimer(0);
		// The module's context cannot be created while this widget's is current: sharing
		// needs the context it shares from to be released first, so everything the editor
		// needs to draw is built inside that window, in one call (see CreateRenderModule).
		this->doneCurrent();
		CreateRenderModule();
		this->makeCurrent();
	}

	void ViewerWidget::CreateRenderModule()
	{
		// The module gets a context of its own, in the same share group as this widget (and,
		// with Qt::AA_ShareOpenGLContexts, the material preview's): the module renders there,
		// the widgets only paste the result. A texture name only means something in the group
		// that created it, which is what lets the material panel paste a frame the module
		// rendered. Without a global share context it shares this widget's, as before.
		QOpenGLContext* shareContext = QOpenGLContext::globalShareContext();
		if (shareContext == nullptr)
		{
			shareContext = context();
		}

		Editor::Core::RenderModuleHost& module = Editor::Core::RenderModuleHost::Create(shareContext);
		if (!module.IsReady())
		{
			// No context of its own (sharing refused, or it could not be made current): build
			// the module on this widget's context, which is current from here on. Nothing is
			// lost visually - only the separation of the two contexts is.
			this->makeCurrent();
			module.BuildOnCurrentContext();
		}
		if (!module.IsReady())
		{
			return;
		}

		// The frame creates the scene view on the module's context (see ViewportFrame); with
		// no context of its own, that is this widget's, and it is current.
		m_frame = std::make_unique<ViewportFrame>(*this);

		// The view was created at its 1x1 construction size, and the layout already happened.
		m_frame->SetSurfaceSize(width(), height());
	}

	void ViewerWidget::paintGL()
	{
		// Jobs the editor queued for the next frame (see CallBackManager): they run before
		// the frame is drawn, on the frame loop, which is the only place they are allowed.
		CallBackManager::instance().exectue();

		if (!m_frame)
		{
			return;
		}
		const ViewportFrame::Result frame = m_frame->Render();
		if (frame.texture == 0)
		{
			return;
		}

		// The frame was drawn on the module's context, which the frame closed again: the
		// paste's read framebuffer belongs to *this* widget's context, so that one has to be
		// current before the blit.
		this->makeCurrent();

		// The window layer's only job on the frame: paste it into its own framebuffer. A blit
		// of the module's colour texture, because drawing it would need the module's quad VAO
		// and vertex arrays are per context. The frame is rendered at the widget's logical
		// size and scaled to the framebuffer here.
		const qreal pixelRatio = devicePixelRatioF();
		m_paster.Paste(
			frame.texture, frame.width, frame.height,
			static_cast<uint32_t>(defaultFramebufferObject()),
			static_cast<int>(width() * pixelRatio),
			static_cast<int>(height() * pixelRatio));
	}

	bool ViewerWidget::event(QEvent* p_event)
	{
		// Every event goes to the frame first, which translates it into the render module's
		// own input event and hands what it does not read to OCCT's interactor - the one
		// tool that still takes Qt events as they are.
		if (m_frame)
		{
			m_frame->HandleEvent(p_event);
		}
		return QOpenGLWidget::event(p_event);
	}

	void ViewerWidget::leaveEvent(QEvent* p_event)
	{
		QOpenGLWidget::leaveEvent(p_event);
		// The hover highlight comes from the picking target, which is only re-read while the
		// cursor moves: a cursor that leaves the viewport produces no more moves, so without
		// this the shape under it stayed lit.
		if (m_frame)
		{
			m_frame->ClearHover();
		}
	}

	void ViewerWidget::timerEvent(QTimerEvent* p_event)
	{
		QOpenGLWidget::timerEvent(p_event);
		// The viewport draws continuously: it is what makes the camera controls, the picking
		// and the job threads' results show up without anyone asking for a repaint.
		this->update();
	}

	void ViewerWidget::resizeEvent(QResizeEvent* p_event)
	{
		QOpenGLWidget::resizeEvent(p_event);
		if (m_frame)
		{
			m_frame->SetSurfaceSize(width(), height());
		}
	}
}
