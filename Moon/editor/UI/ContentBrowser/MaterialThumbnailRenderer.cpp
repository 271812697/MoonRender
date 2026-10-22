#include "MaterialThumbnailRenderer.h"

#include <QFileInfo>

#include <cstdint>
#include <vector>

#include "Core/Global/ServiceLocator.h"
#include "Core/ResourceManagement/MaterialManager.h"
#include "Core/Resources/Material.h"
#include "Core/Rendering/SceneRenderer.h"
#include "Rendering/Context/Driver.h"
#include "Rendering/Context/IGLContextHost.h"
#include "Rendering/HAL/Texture.h"
#include "Rendering/View/EPreviewMesh.h"
#include "Rendering/View/PreviewView.h"
#include "renderer/Context.h"

namespace
{
	/** Binds the render module's context for as long as the object lives, and releases only a
	 * binding it made itself (the same contract as the material designer's load scope). */
	class ModuleContextScope
	{
	public:
		ModuleContextScope()
		{
			auto& context = Core::Global::ServiceLocator::Get<Editor::Core::Context>();
			m_host = context.driver != nullptr ? context.driver->GetContextHost() : nullptr;
			m_ready = m_host != nullptr && m_host->MakeCurrent();
		}

		~ModuleContextScope()
		{
			if (m_ready)
			{
				m_host->DoneCurrent();
			}
		}

		explicit operator bool() const { return m_ready; }

	private:
		Rendering::Context::IGLContextHost* m_host = nullptr;
		bool m_ready = false;
	};
}

namespace MOON
{
	MaterialThumbnailRenderer::MaterialThumbnailRenderer() = default;

	MaterialThumbnailRenderer::~MaterialThumbnailRenderer()
	{
		if (m_view == nullptr)
		{
			return;
		}

		// The view owns GL objects of the module's context; destroying it needs that context.
		ModuleContextScope context;
		m_view.reset();
	}

	QImage MaterialThumbnailRenderer::Render(const QString& p_materialPath, int p_size)
	{
		if (p_size <= 0)
		{
			return {};
		}

		ModuleContextScope context;
		if (!context)
		{
			return {};
		}

		auto& editorContext = Core::Global::ServiceLocator::Get<Editor::Core::Context>();
		if (editorContext.driver == nullptr)
		{
			return {};
		}

		if (m_view == nullptr)
		{
			m_view = std::make_unique<Editor::Panels::PreviewView>(*editorContext.driver);
			m_view->SetPreviewMesh(Editor::Panels::EPreviewMesh::SPHERE);
		}

		// The material designer keys its list with the serializer's resource path.
		const std::string key = (":Materials/" + QFileInfo(p_materialPath).fileName()).toStdString();
		auto* material = Core::Global::ServiceLocator::Get<Core::ResourceManagement::MaterialManager>()
			.GetResource(key, true);
		if (material == nullptr)
		{
			return {};
		}

		m_view->Resize(p_size, p_size);
		m_view->SetMaterial(material);
		m_view->ResetCameraTransform();
		m_view->Update(0.016f);

		// First frame: the view's own sky pass bakes the environment the preview scene uses.
		m_view->Render({});

		// Bind that environment to the material: the preview renderer's cubes, not the
		// viewport's, otherwise the thumbnail would be lit by a different sky.
		auto& renderer = m_view->GetRenderer();
		const auto bind = [material](const char* p_name, Rendering::HAL::Texture* p_texture)
		{
			if (p_texture != nullptr)
			{
				material->SetProperty(p_name, p_texture);
			}
		};
		bind("_IrradianceCube", renderer.GetIrradianceCube());
		bind("_PrefilterCube", renderer.GetPrefilterCube());
		bind("_BRDFLut", renderer.GetBrdfTexture());

		// Second frame with the environment bound.
		m_view->Render({});

		std::vector<uint8_t> pixels;
		int width = 0;
		int height = 0;
		if (!m_view->ReadFrame(pixels, width, height) || width <= 0 || height <= 0)
		{
			return {};
		}

		const QImage frame(pixels.data(), width, height, width * 4, QImage::Format_RGBA8888);
		return frame.copy().scaled(p_size, p_size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
	}
}
