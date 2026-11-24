#include "viewerwidget.h"
#include <Rendering/Overlay/Screen/ScreenOverlayRegistry.h>
#include <unordered_map>
#include "viewportframe.h"
#include "renderer/RenderModuleHost.h"
#include "renderer/QtGLContextHost.h"
#include <cstdlib>
#include <Rendering/Context/IPlatform.h>
#include "Interactive/Im3DRenderer.h"
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
		format.setSamples(0);
		this->setFormat(format);
		RegService(ViewerWidget, *this);
	}

	ViewerWidget::~ViewerWidget()
	{
		// In this order, with the right context current for each part: the paster's framebuffer
		// belongs to this widget's context, while the frame's view only records its releases (the
		// destroy commands run on the executor), so the module's context is never bound here.
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

		// The overlay's 2D widgets ask for a cursor shape (a hand over the view cube, resize
		// arrows over the gizmos); the windowing layer is what puts it on screen. This is that
		// layer, so it installs itself as the sink: the overlay never touches a Qt type.
		MOON::ScreenOverlayRegistry::Instance().SetCursorSink([this](MOON::EScreenCursor p_cursor)
		{
			static const std::unordered_map<MOON::EScreenCursor, Qt::CursorShape> shapes = {
				{ MOON::EScreenCursor::Arrow, Qt::ArrowCursor },
				{ MOON::EScreenCursor::Hand, Qt::PointingHandCursor },
				{ MOON::EScreenCursor::TextInput, Qt::IBeamCursor },
				{ MOON::EScreenCursor::ResizeAll, Qt::SizeAllCursor },
				{ MOON::EScreenCursor::ResizeNS, Qt::SizeVerCursor },
				{ MOON::EScreenCursor::ResizeEW, Qt::SizeHorCursor },
				{ MOON::EScreenCursor::ResizeNESW, Qt::SizeBDiagCursor },
				{ MOON::EScreenCursor::ResizeNWSE, Qt::SizeFDiagCursor },
				{ MOON::EScreenCursor::NotAllowed, Qt::ForbiddenCursor },
			};
			const auto it = shapes.find(p_cursor);
			setCursor(it != shapes.end() ? it->second : Qt::ArrowCursor);
		});

	}

	void ViewerWidget::CreateRenderModule()
	{
		QOpenGLContext* shareContext = nullptr;
		if (Editor::Core::RenderModuleHost::ThreadedMode())
		{
			shareContext = context();
		}
		else
		{
			shareContext = QOpenGLContext::globalShareContext();
			if (shareContext == nullptr)
			{
				shareContext = context();
			}
		}
		auto qtHost = std::make_unique<Editor::Core::QtGLContextHost>();
		qtHost->PrepareSurface(shareContext);
		std::unique_ptr<Rendering::Context::IPlatform> platform = std::move(qtHost);
		Editor::Core::RenderModuleHost& module = Editor::Core::RenderModuleHost::Create(
			std::move(platform),
			shareContext,
			[] {
				MOON::ImRenderer::instance().init();
			});
		if (!module.IsReady())
		{
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
		this->makeCurrent();
		const qreal pixelRatio = devicePixelRatioF();
		m_paster.Paste(
			frame.texture, frame.width, frame.height,
			static_cast<uint32_t>(defaultFramebufferObject()),
			static_cast<int>(width() * pixelRatio),
			static_cast<int>(height() * pixelRatio));
	}

	bool ViewerWidget::event(QEvent* p_event)
	{
		if (m_frame)
		{
			m_frame->HandleEvent(p_event);
		}
		return QOpenGLWidget::event(p_event);
	}

	void ViewerWidget::leaveEvent(QEvent* p_event)
	{
		QOpenGLWidget::leaveEvent(p_event);
		if (m_frame)
		{
			m_frame->ClearHover();
		}
	}
	void ViewerWidget::timerEvent(QTimerEvent* p_event)
	{
		QOpenGLWidget::timerEvent(p_event);
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
