#include "MaterialPreviewWidget.h"

#include <algorithm>
#include <iostream>
#include <chrono>

#include <QMouseEvent>
#include <QSurfaceFormat>
#include <QTimer>
#include <QWheelEvent>

#include <Rendering/View/InputEvent.h>

#include "Core/Global/ServiceLocator.h"
#include "renderer/Context.h"

#include <Core/Resources/Material.h>
#include <Rendering/Context/Driver.h>
#include <Rendering/Context/IGLContextHost.h>
#include <Rendering/Resources/Loaders/ShaderLoader.h>
#include <Rendering/Resources/Shader.h>
#include <Rendering/View/PreviewView.h>

MOON::MaterialPreviewWidget::MaterialPreviewWidget(QWidget* p_parent) :
	QOpenGLWidget(p_parent)
{
	setMinimumHeight(200);
	// Single sample: the view's frame is pasted into this widget's framebuffer with a blit, and
	// blitting into a multisampled framebuffer (even with one sample) is invalid. The view does
	// its own MSAA on the module's side.
	QSurfaceFormat format;
	format.setSamples(0);
	setFormat(format);
}

MOON::MaterialPreviewWidget::~MaterialPreviewWidget()
{
	// The view and the material own GL objects of the module's context; destroying them needs
	// that context current. The view goes first: its scene points at the material.
	auto& editorContext = ::Core::Global::ServiceLocator::Get<::Editor::Core::Context>();
	auto* host = editorContext.driver != nullptr ? editorContext.driver->GetContextHost() : nullptr;
	const bool moduleCurrent = host != nullptr && host->MakeCurrent();
	delete m_view;
	m_view = nullptr;
	m_material.reset();
	if (moduleCurrent)
	{
		host->DoneCurrent();
	}

	// The paster's read framebuffer belongs to this widget's context, so it is deleted while
	// that context is current; the member's destructor then has nothing left to release.
	if (context() != nullptr)
	{
		makeCurrent();
		m_paster.Release();
		doneCurrent();
	}
}

void MOON::MaterialPreviewWidget::SetShaderPackage(const std::string& p_package, const std::string& p_shaderKey)
{
	m_package = p_package;
	m_shaderKey = p_shaderKey;
	++m_packageGeneration;
	// The compile happens on the next paint, where the module's context is current.
	update();
}

const std::string& MOON::MaterialPreviewWidget::GetError() const
{
	return m_error;
}

void MOON::MaterialPreviewWidget::ShowMaterial(::Core::Resources::Material* p_material)
{
	m_shownMaterial = p_material;

	if (m_view != nullptr)
	{
		m_view->SetMaterial(m_shownMaterial != nullptr ? m_shownMaterial : m_material.get());
	}
	update();
}

void MOON::MaterialPreviewWidget::RequestThumbnail()
{
	m_thumbnailRequested = true;
	update();
}

bool MOON::MaterialPreviewWidget::EnsureView()
{
	if (m_view != nullptr)
	{
		return true;
	}

	auto& context = ::Core::Global::ServiceLocator::Get<::Editor::Core::Context>();
	if (context.driver == nullptr)
	{
		return false;
	}

	m_view = new ::Editor::Panels::PreviewView(*context.driver);
	m_material = std::make_unique<::Core::Resources::Material>();
	m_view->SetMaterial(m_shownMaterial != nullptr ? m_shownMaterial : m_material.get());
	return true;
}

void MOON::MaterialPreviewWidget::RefreshShader()
{
	if (m_loadedGeneration == m_packageGeneration)
	{
		return;
	}
	if (m_package.empty())
	{
		m_loadedGeneration = m_packageGeneration;
		return;
	}

	// The shader belongs to the resource manager, keyed by the .ovfx path - the same instance the
	// scene's materials point at when the file is in use. A hot reload replaces its programs in
	// place (ShaderLoader::RecompileFromPackage), so the preview and the viewport switch together
	// and nothing is created twice.
	auto& context = ::Core::Global::ServiceLocator::Get<::Editor::Core::Context>();
	std::string error;
	::Rendering::Resources::Shader* shader = context.shaderManager.GetResource(m_shaderKey, false);

	if (shader == nullptr)
	{
		shader = ::Rendering::Resources::Loaders::ShaderLoader::CreateFromPackage(
			m_package.data(), static_cast<uint32_t>(m_package.size()), m_shaderKey, &error);
		if (shader != nullptr)
		{
			context.shaderManager.RegisterResource(m_shaderKey, shader);
		}
	}
	else if (!::Rendering::Resources::Loaders::ShaderLoader::RecompileFromPackage(
		*shader, m_package.data(), static_cast<uint32_t>(m_package.size()), &error))
	{
		shader = nullptr;
	}

	if (shader == nullptr)
	{
		m_error = error.empty() ? std::string("the driver refused the material") : error;
		// Keep what is on screen: a broken edit must not blank the preview.
		m_loadedGeneration = m_packageGeneration;
		emit shaderFailed(QString::fromStdString(m_error));
		return;
	}

	m_material->SetShader(shader);
	m_loadedGeneration = m_packageGeneration;
	m_error.clear();
}

void MOON::MaterialPreviewWidget::paintGL()
{
	auto& context = ::Core::Global::ServiceLocator::Get<::Editor::Core::Context>();
	auto* host = context.driver != nullptr ? context.driver->GetContextHost() : nullptr;
	if (host == nullptr || !host->MakeCurrent())
	{
		// The viewport creates the render module's context when it is first painted. The panel
		// can be opened from the View menu before that (on the start page), so this paint has
		// nothing to render on: ask for another one shortly instead of staying black for good.
		QTimer::singleShot(500, this, [this] { update(); });
		return;
	}

	if (!EnsureView())
	{
		host->DoneCurrent();
		return;
	}

	RefreshShader();

	// Physical pixels: the view renders at the surface's real resolution and is pasted 1:1.
	const int frameWidth = std::max(1, static_cast<int>(width() * devicePixelRatioF()));
	const int frameHeight = std::max(1, static_cast<int>(height() * devicePixelRatioF()));
	m_view->Resize(frameWidth, frameHeight);

	// The camera controller runs on Update: without this the preview's camera would never move.
	const double nowMs = std::chrono::duration<double, std::milli>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
	const float deltaTime = m_lastPaintMs > 0.0 ? static_cast<float>((nowMs - m_lastPaintMs) / 1000.0) : 0.016f;
	m_lastPaintMs = nowMs;
	m_view->Update(deltaTime);
	// The pressed/released edges the controller just read belong to this frame only; without
	// this a press would look pressed forever and a release would never arrive.
	m_view->ClearEvents();

	// No overlays on purpose: ImGui, the 2D draw list and the gizmos belong to the viewport's
	// frame. What this view draws is its own scene (see PreviewView).
	m_view->Render({});

	// The material browser wants a small image of what is on the sphere. Reading the frame back
	// is a synchronous read, so at most one per burst: a request that arrives while throttled
	// keeps its flag and asks for another paint after the window.
	if (m_thumbnailRequested)
	{
		if (nowMs - m_lastThumbnailMs >= 200.0)
		{
			m_lastThumbnailMs = nowMs;

			int thumbnailWidth = 0;
			int thumbnailHeight = 0;
			if (m_view->ReadFrame(m_thumbnailPixels, thumbnailWidth, thumbnailHeight))
			{
				const QImage frame(m_thumbnailPixels.data(), thumbnailWidth, thumbnailHeight,
					thumbnailWidth * 4, QImage::Format_RGBA8888);
				emit thumbnailReady(frame.copy().scaled(
					96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation));
			}
			m_thumbnailRequested = false;
		}
		else
		{
			QTimer::singleShot(220, this, [this] { update(); });
		}
	}

	const uint32_t frameTexture = m_view->GetFrameTextureID();

	host->DoneCurrent();

	// Back on this widget's context: the blit has to happen here, with the texture shared.
	makeCurrent();
	m_paster.Paste(frameTexture, frameWidth, frameHeight,
		static_cast<uint32_t>(defaultFramebufferObject()), frameWidth, frameHeight);
}

namespace
{
	/** Qt's button to the render module's neutral one. */
	::Editor::Panels::MouseButton ToMouseButton(Qt::MouseButton p_button)
	{
		using ::Editor::Panels::MouseButton;
		switch (p_button)
		{
		case Qt::LeftButton: return MouseButton::MOUSE_BUTTON_LEFT;
		case Qt::MiddleButton: return MouseButton::MOUSE_BUTTON_MIDDLE;
		case Qt::RightButton: return MouseButton::MOUSE_BUTTON_RIGHT;
		default: return MouseButton::MOUSE_BUTTON_LEFT;
		}
	}
}

void MOON::MaterialPreviewWidget::mousePressEvent(QMouseEvent* p_event)
{
	if (m_view != nullptr)
	{
		::Editor::Panels::InputEvent event;
		event.type = ::Editor::Panels::InputEvent::EType::MouseButtonPress;
		event.button = ToMouseButton(p_event->button());
		event.x = static_cast<float>(p_event->pos().x());
		event.y = static_cast<float>(p_event->pos().y());
		m_view->ReceiveEvent(event);
	}
	update();
}

void MOON::MaterialPreviewWidget::mouseMoveEvent(QMouseEvent* p_event)
{
	if (m_view != nullptr)
	{
		::Editor::Panels::InputEvent event;
		event.type = ::Editor::Panels::InputEvent::EType::MouseMove;
		event.x = static_cast<float>(p_event->pos().x());
		event.y = static_cast<float>(p_event->pos().y());
		m_view->ReceiveEvent(event);
	}
	update();
}

void MOON::MaterialPreviewWidget::mouseReleaseEvent(QMouseEvent* p_event)
{
	if (m_view != nullptr)
	{
		::Editor::Panels::InputEvent event;
		event.type = ::Editor::Panels::InputEvent::EType::MouseButtonRelease;
		event.button = ToMouseButton(p_event->button());
		event.x = static_cast<float>(p_event->pos().x());
		event.y = static_cast<float>(p_event->pos().y());
		m_view->ReceiveEvent(event);
	}
	update();
}

void MOON::MaterialPreviewWidget::wheelEvent(QWheelEvent* p_event)
{
	if (m_view != nullptr)
	{
		::Editor::Panels::InputEvent event;
		event.type = ::Editor::Panels::InputEvent::EType::Wheel;
		event.wheelDelta = p_event->angleDelta().y() / 120.0f;
		event.x = static_cast<float>(p_event->pos().x());
		event.y = static_cast<float>(p_event->pos().y());
		m_view->ReceiveEvent(event);
	}
	update();
}