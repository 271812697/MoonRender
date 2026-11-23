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
		// The module gets a context of its own, in the same share group as this widget (and,
		// with Qt::AA_ShareOpenGLContexts, the material preview's): the module renders there,
		// the widgets only paste the result. A texture name only means something in the group
		// that created it, which is what lets the material panel paste a frame the module
		// rendered. Without a global share context it shares this widget's, as before.
		// With a render thread the source is this widget's own context, exactly as the Qt-Filament
		// demo does it: Qt's global share context belongs to the GUI thread's context group, and
		// sharing from it on another thread makes the widget's own makeCurrent trip Qt's thread
		// check. Without a render thread the global context stays as before, so the material
		// preview keeps seeing the module's frames.
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

		// The engine does not know what a QOpenGLContext is: the editor hands it a context through
		// the interface, and supplies the host-side GL objects that belong to that context (the
		// overlay geometry) as the build callback.
		// The engine's module, on a context of its own. The platform entry point
		// (Create(platform, sharedContext, ...), Filament's Platform::createDriver shape) is in
		// place and is what the render thread will use; the live path still hands in a context
		// host, because switching it currently crashes in the first frame - see
		// docs/RenderThreadPlan.md.
		auto qtHost = std::make_unique<Editor::Core::QtGLContextHost>();
		// Qt only lets a QOffscreenSurface be created on the GUI thread, and a surface made on the
		// render thread is invalid: makeCurrent then reports success while every GL call on that
		// thread lands on a drawable that cannot be drawn to (that is the crash at the first
		// glDrawElements). The surface the render thread will use is therefore prepared here, on
		// the GUI thread, before the engine starts - see QtGLContextHost::PrepareSurface.
		qtHost->PrepareSurface(shareContext);
		std::unique_ptr<Rendering::Context::IPlatform> platform = std::move(qtHost);
		Editor::Core::RenderModuleHost& module = Editor::Core::RenderModuleHost::Create(
			std::move(platform),
			shareContext,
			[] {
				// The overlay's host-side objects only (see ImRenderer::init). The gizmo's geometry
				// is built on the first frame instead (see
				// ViewportFrame::Render), where the context that owns it is current and the uploads
				// it causes can travel the same path as the draws that need them.
				//MOON::ImRenderer::instance().init();
			});
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
