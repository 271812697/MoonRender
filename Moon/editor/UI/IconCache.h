#pragma once

#include <QString>
#include <QHash>

#include <Rendering/Overlay/Im2D.h>

namespace MOON
{
	/** Caches the GL textures the 2D overlay draws icons from.
	 *
	 * Im2D can already blit a texture (`DrawList::AddImage`), but nothing in the project turns an
	 * SVG into one. This rasterizes a resource SVG at the requested pixel size with QSvgRenderer,
	 * uploads it once and hands back the texture name, so a widget can draw an icon with a tint -
	 * which is what lets a button recolour on hover without a second asset.
	 *
	 * The texture is created on first use, which happens while the render context is current
	 * (a widget draws inside the overlay pass). */
	class IconCache
	{
	public:
		static IconCache& Instance();

		/** Texture for p_path at p_pixelSize (already multiplied by the UI scale by the caller).
		 * Returns 0 when the SVG cannot be loaded. */
		::Rendering::Overlay::Im2D::TextureId Get(const QString& p_path, int p_pixelSize);

	private:
		IconCache() = default;
		QHash<QString, ::Rendering::Overlay::Im2D::TextureId> mTextures;
	};
}
