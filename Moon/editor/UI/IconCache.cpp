#include "editor/UI/IconCache.h"

#include "editor/UiScale.h"

#include <QImage>
#include <QPainter>
#include <QSvgRenderer>

#include <glad/glad.h>

MOON::IconCache& MOON::IconCache::Instance()
{
	static IconCache cache;
	return cache;
}

::Rendering::Overlay::Im2D::TextureId MOON::IconCache::Get(const QString& p_path, int p_pixelSize)
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
		mTextures.insert(key, 0);
		return 0;
	}

	QImage image(pixels, pixels, QImage::Format_RGBA8888);
	image.fill(Qt::transparent);
	QPainter painter(&image);
	painter.setRenderHint(QPainter::Antialiasing, true);
	renderer.render(&painter);
	painter.end();

	GLuint texture = 0;
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, pixels, pixels, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.constBits());
	glBindTexture(GL_TEXTURE_2D, 0);

	const auto id = static_cast<::Rendering::Overlay::Im2D::TextureId>(texture);
	mTextures.insert(key, id);
	return id;
}
