#include "MaterialThumbnailRenderer.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
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

		// The CAD materials read a per-face domain id and sample a domain colour table with it: on
		// the sphere that path reads garbage and the thumbnail comes out black, so they are shown
		// on the CAD part instead.
		// The material designer keys its list with the serializer's resource path.
		const std::string key = (":Materials/" + QFileInfo(p_materialPath).fileName()).toStdString();
		auto* material = Core::Global::ServiceLocator::Get<Core::ResourceManagement::MaterialManager>()
			.GetResource(key, true);
		if (material == nullptr)
		{
			return {};
		}

		m_view->Resize(p_size, p_size);
		const bool isCadMaterial = [material] {
			auto* shader = material->GetShader();
			if (shader == nullptr) { return false; }
			for (const auto& parameter : shader->GetParameters())
			{
				if (parameter.name == "domainColorTex") { return true; }
			}
			return false;
		}();
		m_view->SetPreviewMesh(isCadMaterial ? Editor::Panels::EPreviewMesh::CAD : Editor::Panels::EPreviewMesh::SPHERE);
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
		const QImage scaled = frame.copy().scaled(p_size, p_size, Qt::KeepAspectRatio, Qt::SmoothTransformation);

		// MOON_DUMP_THUMBNAILS=<dir>: writes the preview scene the material editor shows, so it can
		// be inspected without the UI. MOON_DUMP_SIZE renders the dump larger.
		if (const char* p_dir = std::getenv("MOON_DUMP_THUMBNAILS"); p_dir != nullptr && *p_dir != '\0')
		{
			std::error_code error;
			std::filesystem::create_directories(p_dir, error);
			int dumpSize = 0;
			if (const char* p_sizeText = std::getenv("MOON_DUMP_SIZE"); p_sizeText != nullptr)
			{
				dumpSize = std::atoi(p_sizeText);
			}
			QImage dump = scaled;
			if (dumpSize > p_size)
			{
				m_view->Resize(dumpSize, dumpSize);
				// The camera pose is a destination the view eases towards; settle it so the dump
				// honours MOON_PREVIEW_CAM_ELEV.
				for (int settle = 0; settle < 120; ++settle)
				{
					m_view->Update(0.05f);
				}
				m_view->Render({});
				m_view->Render({});
				std::vector<uint8_t> dumpPixels;
				int dumpWidth = 0;
				int dumpHeight = 0;
				if (m_view->ReadFrame(dumpPixels, dumpWidth, dumpHeight) && dumpWidth > 0 && dumpHeight > 0)
				{
					dump = QImage(dumpPixels.data(), dumpWidth, dumpHeight, dumpWidth * 4, QImage::Format_RGBA8888).copy();
				}
			}
			const QString name = QFileInfo(p_materialPath).fileName() + QStringLiteral(".png");
			dump.save(QString::fromLocal8Bit(p_dir) + QStringLiteral("/") + name);
		}
		return scaled;
	}
	namespace
	{
		/** One table for the whole editor: the content browser fills it, the property panel reads it.
		 * Deliberately never freed - it holds nothing but images, and a QPixmap table destroyed
		 * after the GUI is gone is a shutdown hazard. */
		QHash<QString, QPixmap>& ThumbnailTable()
		{
			static QHash<QString, QPixmap>* table = new QHash<QString, QPixmap>();
			return *table;
		}
	}

	QString MaterialThumbnailKey(const QString& p_materialPath)
	{
		// Cleaned, so the two callers agree whatever separators they built the path with: the
		// browser appends "/Moon/Data/Engine", std::filesystem on Windows writes backslashes.
		const QFileInfo info(p_materialPath);
		return QDir::cleanPath(info.absoluteFilePath()) + QLatin1Char('|')
			+ QString::number(info.lastModified().toMSecsSinceEpoch());
	}

	QPixmap CachedMaterialThumbnail(const QString& p_materialPath)
	{
		return ThumbnailTable().value(MaterialThumbnailKey(p_materialPath));
	}

	void StoreMaterialThumbnail(const QString& p_materialPath, const QPixmap& p_image)
	{
		ThumbnailTable().insert(MaterialThumbnailKey(p_materialPath), p_image);
	}

}
