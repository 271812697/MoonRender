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
#include "renderer/SceneView.h"

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

void MOON::MaterialPreviewWidget::ResetView()
{
	if (m_view != nullptr)
	{
		m_view->ResetCameraTransform();
	}
	update();
}

void MOON::MaterialPreviewWidget::RequestThumbnail()
{
	m_thumbnailRequested = true;
	update();
}

void MOON::MaterialPreviewWidget::SetPreviewMesh(Editor::Panels::EPreviewMesh p_mesh)
{
	// Only remember it here. Rebuilding the mesh creates GL objects (VAOs, buffers, and for the
	// loaded ball a whole model), which needs the render module's context current - this is a
	// widget slot with Qt's context, not the module's. paintGL applies the pending mesh with the
	// right context current (see there).
	m_previewMesh = p_mesh;
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
	m_view->SetPreviewMesh(m_previewMesh);
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

	// The shader belongs to the resource manager, keyed by the .shader path - the same instance the
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

void MOON::MaterialPreviewWidget::BindEnvironmentTextures()
{
	// The preview has its own scene and sky pass (a studio HDR, see PreviewView): bind that
	// renderer's irradiance/prefilter cubes, not the viewport's, or the preview would show the
	// scene's sky instead of its own environment.
	auto& renderer = m_view->GetRenderer();

	auto* irradiance = renderer.GetIrradianceCube();
	auto* prefilter = renderer.GetPrefilterCube();
	// The DFG LUT is bound view-globally by CompositeRenderer (see ABaseRenderer::SetBrdfLut);
	// the material must not carry it as a property, or an instance without it samples white.
	auto* brdf = renderer.GetBrdfTexture();
	if (irradiance == nullptr || prefilter == nullptr || brdf == nullptr)
	{
		// The sky pass has not captured them yet (the viewport has not drawn). The preview may
		// have painted first, so ask for another paint shortly instead of staying unlit.
		QTimer::singleShot(200, this, [this] { update(); });
		return;
	}

	const auto bind = [](::Core::Resources::Material* p_material, const char* p_name,
		::Rendering::HAL::Texture* p_texture)
	{
		if (p_material == nullptr)
		{
			return;
		}
		if (const auto property = p_material->GetProperty(p_name))
		{
			if (const auto handle = std::get_if<::Rendering::HAL::TextureHandle*>(&property->value);
				handle != nullptr && *handle == p_texture)
			{
				return; // already pointing at it; SetProperty would bump the upload version
			}
		}
		// Set (not TrySet): the internal IBL parameters are declared ui:false and a material
		// instance can come back from the loader without them, in which case TrySetProperty is
		// a silent no-op and the shader samples the white fallback (the DFG LUT then reads
		// (1,1) and kills the diffuse).
		p_material->SetProperty(p_name, p_texture);
	};

	if (!m_refractionCube)
	{
		// One dark texel per face: the preview backdrop, used as the refraction source.
		m_refractionCube = std::make_unique<::Rendering::HAL::Texture>(
			::Rendering::Settings::ETextureType::TEXTURE_CUBE, "PreviewRefraction");
		m_refractionCube->Allocate(::Rendering::Settings::TextureDesc{
			.width = 1, .height = 1,
			.minFilter = ::Rendering::Settings::ETextureFilteringMode::NEAREST,
			.magFilter = ::Rendering::Settings::ETextureFilteringMode::NEAREST,
			.horizontalWrap = ::Rendering::Settings::ETextureWrapMode::CLAMP_TO_EDGE,
			.verticalWrap = ::Rendering::Settings::ETextureWrapMode::CLAMP_TO_EDGE,
			.internalFormat = ::Rendering::Settings::EInternalFormat::RGBA32F,
			.useMipMaps = false });
		const float backdrop[4] = { 0.032f, 0.032f, 0.034f, 1.0f };
		m_refractionCube->Upload(backdrop, ::Rendering::Settings::EFormat::RGBA,
			::Rendering::Settings::EPixelDataType::FLOAT);
	}

	for (::Core::Resources::Material* material : { m_shownMaterial, m_material.get() })
	{
		bind(material, "_IrradianceCube", irradiance);
		bind(material, "_PrefilterCube", prefilter);
		bind(material, "_RefractionCube", m_refractionCube.get());
	}
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
	BindEnvironmentTextures();

	// Apply a mesh the panel picked since the last paint. It has to happen here: SetPreviewMesh
	// itself runs on a Qt slot, where the module's context is not current, and building the mesh
	// creates VAOs/buffers (and can load a model).
	if (m_view->GetPreviewMesh() != m_previewMesh)
	{
		m_view->SetPreviewMesh(m_previewMesh);
		m_thumbnailRequested = true;
	}

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
	// A camera move is an animation: keep repainting until it lands, otherwise the
	// lerp advances one frame per button click and appears not to reset at all.
	if (m_view->IsCameraMoving())
	{
		QTimer::singleShot(16, this, [this] { update(); });
	}
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