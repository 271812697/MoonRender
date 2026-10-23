#include "editor/UI/IconCache.h"

#include "editor/UiScale.h"

#include <QImage>
#include <QPainter>
#include <QSvgRenderer>
#include <QDebug>

#include <glad/glad.h>

#include <Rendering/Resources/Loaders/TextureLoader.h>
#include <Rendering/Resources/Texture.h>

MOON::IconCache& MOON::IconCache::Instance()
{
	static IconCache cache;
	return cache;
}

::Rendering::Resources::Texture* MOON::IconCache::GetTexture(const QString& p_path, int p_pixelSize)
{
	const int pixels = std::max(8, static_cast<int>(p_pixelSize * MOON::UiScale()));
	const QString key = QStringLiteral("%1|%2").arg(p_path).arg(pixels);
	const auto cached = mTextures.constFind(key);
	if (cached != mTextures.constEnd())
	{
		return cached.value();
	}

	QSvgRenderer renderer(p_path);
	if (!renderer.isValid())
	{
		// A wrong resource path is silent otherwise - the icon simply never appears.
		qWarning("[IconCache] cannot load %s", qPrintable(p_path));
		mTextures.insert(key, nullptr);
		return nullptr;
	}

	QImage image(pixels, pixels, QImage::Format_RGBA8888);
	image.fill(Qt::transparent);
	QPainter painter(&image);
	painter.setRenderHint(QPainter::Antialiasing, true);
	renderer.render(&painter);
	painter.end();

	auto* texture = Rendering::Resources::Loaders::TextureLoader::CreateMutable(
		static_cast<uint32_t>(image.width()),
		static_cast<uint32_t>(image.height()),
		image.constBits(),
		Rendering::Settings::ETextureFilteringMode::LINEAR,
		Rendering::Settings::ETextureFilteringMode::LINEAR,
		Rendering::Settings::ETextureWrapMode::CLAMP_TO_EDGE,
		Rendering::Settings::ETextureWrapMode::CLAMP_TO_EDGE,
		false);
	mTextures.insert(key, texture);
	return texture;
}

::Rendering::Overlay::Im2D::TextureId MOON::IconCache::Get(const QString& p_path, int p_pixelSize)
{
	Rendering::Resources::Texture* texture = GetTexture(p_path, p_pixelSize);
	if (texture == nullptr)
	{
		return 0;
	}
	return static_cast<::Rendering::Overlay::Im2D::TextureId>(texture->GetTexture().GetID());
}
