#include "Rendering/Overlay/Im2DFont.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <unordered_map>

namespace
{
	/** Room per size atlas. Text sizes in the editor run from about 12 to 40 px and a Latin +
	 * Chinese label set fits in 1024^2 with plenty of room; going bigger would cost 4x the
	 * upload for the rare case of a large glyph count. */
	constexpr int kAtlasWidth = 1024;
	constexpr int kAtlasHeight = 1024;
	/** One transparent pixel of padding between glyphs: the atlas is sampled with linear
	 * filtering, and a glyph that touches its neighbour picks up its coverage otherwise. */
	constexpr int kGlyphPadding = 1;
	/** Sizes are quantized to whole pixels; below this text is unreadable anyway, above it the
	 * sheets get expensive. */
	constexpr float kMinSize = 6.0f;
	constexpr float kMaxSize = 128.0f;

	/** The UTF-8 decoder Im2D uses (the same rules ImGui applies): returns the codepoint and
	 * advances p_text. Malformed bytes are returned as themselves, which keeps the pen moving
	 * instead of stalling the loop. */
	uint32_t DecodeUtf8(const char*& p_text, const char* p_textEnd)
	{
		const auto* s = reinterpret_cast<const unsigned char*>(p_text);
		unsigned int c = s[0];
		int extra = 0;
		if (c < 0x80)
		{
			p_text += 1;
			return c;
		}
		if ((c & 0xE0) == 0xC0) { c &= 0x1F; extra = 1; }
		else if ((c & 0xF0) == 0xE0) { c &= 0x0F; extra = 2; }
		else if ((c & 0xF8) == 0xF0) { c &= 0x07; extra = 3; }
		else
		{
			// Invalid lead byte: consume it, and let the caller draw the fallback glyph.
			p_text += 1;
			return 0xFFFD;
		}

		if (p_text + extra >= p_textEnd + 1)
		{
			p_text = p_textEnd;
			return 0xFFFD;
		}
		for (int i = 1; i <= extra; ++i)
		{
			const unsigned int next = s[i];
			if ((next & 0xC0) != 0x80)
			{
				p_text += i;
				return 0xFFFD;
			}
			c = (c << 6) | (next & 0x3F);
		}
		p_text += extra + 1;
		return c;
	}
}

struct Rendering::Overlay::Im2D::Font::Impl
{
	/** One atlas: everything rasterized at a single pixel size. */
	struct SizeAtlas
	{
		float pixelSize = 0.0f;
		std::vector<uint8_t> pixels
			= std::vector<uint8_t>(static_cast<size_t>(kAtlasWidth) * kAtlasHeight * 4, 0);
		int cursorX = 0;
		int cursorY = 0;
		int rowHeight = 0;
		bool full = false;
		bool dirty = false;
		TextureId textureId = 0;
		std::unordered_map<uint32_t, Glyph> glyphs;
	};

	FT_Library library = nullptr;
	std::vector<FT_Face> faces;
	float referenceSize = 24.0f;
	std::vector<SizeAtlas> atlases;
	/** Per-atlas glyph cache: atlas index -> codepoint -> glyph. */
	std::unordered_map<int, std::unordered_map<uint32_t, Glyph>> cache;

	FT_Face FaceForCodepoint(uint32_t p_codepoint) const
	{
		for (FT_Face face : faces)
		{
			if (FT_Get_Char_Index(face, p_codepoint) != 0)
			{
				return face;
			}
		}
		return nullptr;
	}

	int AtlasForSize(float p_size)
	{
		const int rounded = static_cast<int>(std::lround(
			std::clamp(p_size, kMinSize, kMaxSize)));
		for (int i = 0; i < static_cast<int>(atlases.size()); ++i)
		{
			if (static_cast<int>(std::lround(atlases[i].pixelSize)) == rounded)
			{
				return i;
			}
		}
		SizeAtlas atlas;
		atlas.pixelSize = static_cast<float>(rounded);
		atlases.push_back(std::move(atlas));
		return static_cast<int>(atlases.size()) - 1;
	}

	/** Reserves p_width x p_height texels (plus padding) and returns the top-left corner. */
	static bool Reserve(SizeAtlas& p_atlas, int p_width, int p_height, int& p_outX, int& p_outY)
	{
		if (p_atlas.full)
		{
			return false;
		}
		const int w = p_width + 2 * kGlyphPadding;
		const int h = p_height + 2 * kGlyphPadding;
		if (p_atlas.cursorX + w > kAtlasWidth)
		{
			p_atlas.cursorX = 0;
			p_atlas.cursorY += p_atlas.rowHeight;
			p_atlas.rowHeight = 0;
		}
		if (p_atlas.cursorY + h > kAtlasHeight)
		{
			p_atlas.full = true;
			return false;
		}
		p_outX = p_atlas.cursorX + kGlyphPadding;
		p_outY = p_atlas.cursorY + kGlyphPadding;
		p_atlas.cursorX += w;
		p_atlas.rowHeight = p_atlas.rowHeight > h ? p_atlas.rowHeight : h;
		return true;
	}

	const Glyph* Bake(int p_atlasIndex, uint32_t p_codepoint)
	{
		SizeAtlas& atlas = atlases[static_cast<size_t>(p_atlasIndex)];
		FT_Face face = FaceForCodepoint(p_codepoint);
		if (face == nullptr)
		{

			return nullptr;
		}

		FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(std::lround(atlas.pixelSize)));
		// Light hinting: full hinting mangles the stem widths of a UI font at 12-20 px, no
		// hinting at all lets them wobble; light keeps the shapes and still snaps the stems.
		if (FT_Load_Char(face, p_codepoint, FT_LOAD_RENDER | FT_LOAD_TARGET_LIGHT) != 0)
		{
			return nullptr;
		}

		const FT_GlyphSlot slot = face->glyph;
		const FT_Bitmap& bitmap = slot->bitmap;
		// Only 8-bit coverage is supported; freetype renders normal targets that way, and the
		// monochrome/2-bit modes are not requested anywhere.
		if (bitmap.pixel_mode != FT_PIXEL_MODE_GRAY)
		{
			std::cerr << "[Im2D] unsupported glyph bitmap mode " << bitmap.pixel_mode << std::endl;
			return nullptr;
		}

		const int width = static_cast<int>(bitmap.width);
		const int height = static_cast<int>(bitmap.rows);
		int atlasX0 = 0;
		int atlasY0 = 0;

		if (width > 0 && height > 0 && !Reserve(atlas, width, height, atlasX0, atlasY0))
		{
			std::cerr << "[Im2D] glyph atlas " << p_atlasIndex << " is full; '"
				<< static_cast<char>(p_codepoint) << "' is missing" << std::endl;
			return nullptr;
		}

		const int stride = bitmap.pitch > 0 ? bitmap.pitch : -bitmap.pitch;
		for (int y = 0; y < height; ++y)
		{
			const uint8_t* src = bitmap.buffer + y * stride;
			uint8_t* dst = atlas.pixels.data()
				+ (static_cast<size_t>(atlasY0 + y) * kAtlasWidth + atlasX0) * 4;
			for (int x = 0; x < width; ++x)
			{
				const uint8_t coverage = src[x];
				dst[x * 4 + 0] = 255;
				dst[x * 4 + 1] = 255;
				dst[x * 4 + 2] = 255;
				dst[x * 4 + 3] = coverage;
			}
		}
		if (width > 0 && height > 0)
		{
			atlas.dirty = true;
		}

		Glyph glyph;
		glyph.codepoint = p_codepoint;
		// 26.6 fixed point: the advance keeps its fractional part, which is what makes spacing
		// even instead of every glyph rounded to a whole pixel.
		glyph.advanceX = static_cast<float>(slot->advance.x) / 64.0f;
		glyph.offsetX = static_cast<float>(slot->bitmap_left);
		// Screen y points down, the bitmap's top is above the baseline.
		glyph.offsetY = -static_cast<float>(slot->bitmap_top);
		glyph.width = static_cast<float>(width);
		glyph.height = static_cast<float>(height);
		glyph.u0 = static_cast<float>(atlasX0) / kAtlasWidth;
		glyph.v0 = static_cast<float>(atlasY0) / kAtlasHeight;
		glyph.u1 = static_cast<float>(atlasX0 + width) / kAtlasWidth;
		glyph.v1 = static_cast<float>(atlasY0 + height) / kAtlasHeight;
		glyph.atlasIndex = p_atlasIndex;

		return &(atlas.glyphs[p_codepoint] = glyph);
	}

	FT_Face ReferenceFace() const { return faces.empty() ? nullptr : faces.front(); }
};

Rendering::Overlay::Im2D::Font::Font()
	: m_impl(std::make_unique<Impl>())
{
}

Rendering::Overlay::Im2D::Font::~Font()
{
	for (FT_Face face : m_impl->faces)
	{
		FT_Done_Face(face);
	}
	if (m_impl->library != nullptr)
	{
		FT_Done_FreeType(m_impl->library);
	}
}

bool Rendering::Overlay::Im2D::Font::Load(const std::vector<std::string>& p_paths, float p_referenceSize)
{
	if (p_paths.empty())
	{
		return false;
	}
	if (FT_Init_FreeType(&m_impl->library) != 0)
	{
		std::cerr << "[Im2D] FT_Init_FreeType failed" << std::endl;
		return false;
	}
	m_impl->referenceSize = p_referenceSize;

	for (const std::string& path : p_paths)
	{
		FT_Face face = nullptr;
		if (FT_New_Face(m_impl->library, path.c_str(), 0, &face) != 0)
		{
			// A fallback that is missing on this machine must not take the primary down with
			// it; the primary failing is what Load() reports.
			std::cerr << "[Im2D] cannot open font '" << path << "'" << std::endl;
			continue;
		}
		FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(std::lround(p_referenceSize)));
		m_impl->faces.push_back(face);
	}

	if (m_impl->faces.empty())
	{
		return false;
	}

	// Bake the printable ASCII range of the reference size up front: it is what every label
	// starts with, and doing it here keeps the first frames from re-uploading the atlas glyph
	// by glyph.
	for (uint32_t c = 32; c < 127; ++c)
	{
		FindGlyph(c, p_referenceSize);
	}
	return true;
}

bool Rendering::Overlay::Im2D::Font::IsLoaded() const
{
	return !m_impl->faces.empty();
}

const Rendering::Overlay::Im2D::Glyph* Rendering::Overlay::Im2D::Font::FindGlyph(uint32_t p_codepoint, float p_size)
{
	const int atlasIndex = m_impl->AtlasForSize(p_size);
	auto& cache = m_impl->cache[atlasIndex];
	const auto it = cache.find(p_codepoint);
	if (it != cache.end())
	{
		return &it->second;
	}
	const Glyph* baked = m_impl->Bake(atlasIndex, p_codepoint);
	if (baked == nullptr)
	{
		return nullptr;
	}
	cache[p_codepoint] = *baked;
	return &cache[p_codepoint];
}

float Rendering::Overlay::Im2D::Font::GetAscent(float p_size)
{
	const FT_Face face = m_impl->ReferenceFace();
	if (face == nullptr)
	{
		return 0.0f;
	}
	FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(std::lround(
		std::clamp(p_size, kMinSize, kMaxSize))));
	return static_cast<float>(face->size->metrics.ascender) / 64.0f;
}

float Rendering::Overlay::Im2D::Font::GetDescent(float p_size)
{
	const FT_Face face = m_impl->ReferenceFace();
	if (face == nullptr)
	{
		return 0.0f;
	}
	FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(std::lround(
		std::clamp(p_size, kMinSize, kMaxSize))));
	return -static_cast<float>(face->size->metrics.descender) / 64.0f;
}

float Rendering::Overlay::Im2D::Font::GetLineHeight(float p_size)
{
	const FT_Face face = m_impl->ReferenceFace();
	if (face == nullptr)
	{
		return 0.0f;
	}
	FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(std::lround(
		std::clamp(p_size, kMinSize, kMaxSize))));
	return static_cast<float>(face->size->metrics.height) / 64.0f;
}

int Rendering::Overlay::Im2D::Font::GetAtlasCount() const
{
	return static_cast<int>(m_impl->atlases.size());
}

int Rendering::Overlay::Im2D::Font::GetAtlasWidth(int) const { return kAtlasWidth; }
int Rendering::Overlay::Im2D::Font::GetAtlasHeight(int) const { return kAtlasHeight; }

float Rendering::Overlay::Im2D::Font::GetAtlasSize(int p_atlasIndex) const
{
	if (p_atlasIndex < 0 || p_atlasIndex >= static_cast<int>(m_impl->atlases.size()))
	{
		return 0.0f;
	}
	return m_impl->atlases[static_cast<size_t>(p_atlasIndex)].pixelSize;
}

const std::vector<uint8_t>& Rendering::Overlay::Im2D::Font::GetAtlasPixels(int p_atlasIndex) const
{
	static const std::vector<uint8_t> empty;
	if (p_atlasIndex < 0 || p_atlasIndex >= static_cast<int>(m_impl->atlases.size()))
	{
		return empty;
	}
	return m_impl->atlases[static_cast<size_t>(p_atlasIndex)].pixels;
}

bool Rendering::Overlay::Im2D::Font::ConsumeAtlasDirty(int p_atlasIndex)
{
	if (p_atlasIndex < 0 || p_atlasIndex >= static_cast<int>(m_impl->atlases.size()))
	{
		return false;
	}
	auto& atlas = m_impl->atlases[static_cast<size_t>(p_atlasIndex)];
	const bool dirty = atlas.dirty;
	atlas.dirty = false;
	return dirty;
}

Rendering::Overlay::Im2D::TextureId Rendering::Overlay::Im2D::Font::GetTextureId(int p_atlasIndex) const
{
	if (p_atlasIndex < 0 || p_atlasIndex >= static_cast<int>(m_impl->atlases.size()))
	{
		return 0;
	}
	return m_impl->atlases[static_cast<size_t>(p_atlasIndex)].textureId;
}

void Rendering::Overlay::Im2D::Font::SetTextureId(int p_atlasIndex, TextureId p_texture)
{
	if (p_atlasIndex < 0 || p_atlasIndex >= static_cast<int>(m_impl->atlases.size()))
	{
		return;
	}
	m_impl->atlases[static_cast<size_t>(p_atlasIndex)].textureId = p_texture;
}

// The UTF-8 decoder lives in this translation unit because it is only used by text drawing, but
// it is declared here so the draw list can share it (see Im2D.cpp).
namespace Rendering::Overlay::Im2D
{
	uint32_t Im2DDecodeUtf8(const char*& p_text, const char* p_textEnd)
	{
		return DecodeUtf8(p_text, p_textEnd);
	}
}
