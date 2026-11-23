#include "editor/ViewCubeLabels.h"

#include <Rendering/Overlay/ViewCubeLabels.h>

#include <vector>

#include "editor/Language.h"

#include <algorithm>
#include <cstdlib>
#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QVector>

namespace
{
	bool gDirty = true;

	/** The rectangle a face samples is narrower than the square it is drawn on, so glyphs come
	 * out squeezed horizontally; the atlas is pre-stretched by this much to cancel it. */
	constexpr qreal kStretchX = 1.0;

	struct FaceLabel
	{
		int column;
		int row;
		const char* english;
		const char* chinese;
		QColor color;
		int rotationDeg;
	};

	/** Where each face is in the atlas, and what it says. The colours match the axes the face
	 * belongs to (X red, Y green, Z blue), which is how the cube reads as a coordinate system. */
	const QVector<FaceLabel>& Faces()
	{
		static const QVector<FaceLabel> faces = {
			{ 0, 0, "L",  "\xe5\xb7\xa6",  QColor(214, 74, 74),  0 },
			{ 0, 1, "R", "\xe5\x8f\xb3",  QColor(214, 74, 74),  0 },
			{ 1, 0, "D",  "\xe4\xb8\x8b",  QColor(104, 168, 66), 90 },
			{ 1, 1, "U",    "\xe4\xb8\x8a",  QColor(104, 168, 66), 90 },
			{ 2, 0, "B",  "\xe5\x90\x8e",  QColor(64, 116, 200), 0 },
			{ 2, 1, "F", "\xe5\x89\x8d",  QColor(64, 116, 200), 0 },
		};
		return faces;
	}
}

QImage MOON::ViewCubeLabels::BuildAtlas(int p_cellPixels)
{
	const int cell = p_cellPixels > 0 ? p_cellPixels : 512;
	QImage atlas(cell * 3, cell * 2, QImage::Format_RGBA8888);
	// Transparent *white*, not transparent black: the cube shader multiplies the albedo map by the
	// face colour, so a transparent texel written as (0,0,0,0) turns the face black. The baked
	// atlas this replaces had the same white-under-zero-alpha layout.
	atlas.fill(QColor(255, 255, 255, 0));

	QPainter painter(&atlas);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);
	// The label has to read at the size the cube is drawn at, which is far smaller than the
	// atlas; the atlas is generated at a multiple of that size so the downscale stays crisp.
	QFont font = painter.font();
	font.setBold(true);
	// The cube maps only the middle of each cell onto the flat centre of a face; the chamfer
	// quads take the rest. The label therefore has to be small - roughly a quarter of the cell.
	// A single letter can be drawn much larger than a whole word before it leaves the rectangle
	// the face samples, so the two languages get their own size.
	const bool englishUi = MOON::Language::Current() != MOON::Language::ELanguage::Chinese;
	font.setPixelSize(static_cast<int>(cell * (englishUi ? 0.52 : 0.46)));

	for (const FaceLabel& face : Faces())
	{
		const bool chinese = MOON::Language::Current() == MOON::Language::ELanguage::Chinese;
		const QString text = QString::fromUtf8(chinese ? face.chinese : face.english);
		painter.save();
		painter.setFont(font);
		painter.setPen(face.color);
		const QRect cellRect(face.column * cell, face.row * cell, cell, cell);
		// Shrink until the word fits the cell with a margin: "R" is far wider than "U", and
		// an atlas where one label runs into the next face reads as a defect.
		QFont fitted = font;
		for (int guard = 0; guard < 12; ++guard)
		{
			const QFontMetrics metrics(fitted);
			if (metrics.horizontalAdvance(text) * kStretchX <= cell * 0.62)
			{
				break;
			}
			fitted.setPixelSize(std::max(8, fitted.pixelSize() - 2));
		}
		painter.setFont(fitted);
		QFontMetrics metrics(fitted);
		painter.scale(kStretchX, 1.0);
		painter.translate(cellRect.center());
		if (face.rotationDeg != 0)
		{
			painter.rotate(face.rotationDeg);
		}
		const QRect textRect = metrics.boundingRect(text);
		painter.drawText(QPointF(-textRect.width() * 0.5 - textRect.left(),
			textRect.height() * 0.5 - textRect.bottom()), text);
		painter.restore();
	}
	// TEMPORARY: with MOON_VCUBE_DEBUG the atlas shows the sub-rectangle the cube actually samples
	// (the front face reads u 0.033-0.30, v 0.05-0.45 of a cell), so the label placement can be
	// checked against the geometry instead of guessed.
	if (std::getenv("MOON_VCUBE_DEBUG") != nullptr)
	{
		painter.setBrush(Qt::NoBrush);
		painter.setPen(QPen(QColor(255, 0, 255), 3));
		for (const FaceLabel& face : Faces())
		{
			const QRectF window(face.column * cell + cell * 0.033, face.row * cell + cell * 0.55,
				cell * 0.267, cell * 0.40);
			painter.drawRect(window);
		}
	}
	painter.end();
	return atlas;
}

void MOON::ViewCubeLabels::FaceUvRect(int p_face, float& p_x, float& p_y, float& p_w, float& p_h)
{
	// The numbers live in the engine now (the overlay geometry reads them too); this forwards.
	::Rendering::Overlay::ViewCubeLabels::FaceUvRect(p_face, p_x, p_y, p_w, p_h);
}
void MOON::ViewCubeLabels::Invalidate()
{
	gDirty = true;
}

bool MOON::ViewCubeLabels::IsDirty()
{
	return gDirty;
}

void MOON::ViewCubeLabels::MarkClean()
{
	gDirty = false;
}

namespace
{
	/** Hands the engine the painter it cannot have: the overlay asks for the atlas pixels through
	 * Rendering::Overlay::ViewCubeLabels (see BuildAtlas there), and the editor is the side that knows
	 * how to draw text. */
	struct AtlasBuilderRegistration
	{
		AtlasBuilderRegistration()
		{
			::Rendering::Overlay::ViewCubeLabels::SetAtlasBuilder(
				[](int p_cellPixels, std::vector<uint8_t>& p_rgba, uint32_t& p_width, uint32_t& p_height)
				{
					QImage atlas = MOON::ViewCubeLabels::BuildAtlas(p_cellPixels);
					// The file textures go through stb_image with a vertical flip, so the generated
					// one is flipped the same way for the cube's UVs to line up.
					atlas = atlas.mirrored(false, true);
					atlas = atlas.convertToFormat(QImage::Format_RGBA8888);
					p_width = static_cast<uint32_t>(atlas.width());
					p_height = static_cast<uint32_t>(atlas.height());
					const auto* bits = static_cast<const uint8_t*>(atlas.constBits());
					p_rgba.assign(bits, bits + static_cast<size_t>(p_width) * p_height * 4);
				});
		}
	};

	const AtlasBuilderRegistration g_atlasBuilderRegistration;
}
