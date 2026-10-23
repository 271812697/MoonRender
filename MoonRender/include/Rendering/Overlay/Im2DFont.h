#pragma once

// Im2D's font: a freetype-backed glyph cache and its atlases.
//
// ImGui builds one atlas for one size and scales it for everything else. That is why text
// drawn at a size other than the baked one looks soft: the glyphs are magnified or minified by
// a bilinear filter. This font does what ImGui 1.92 does instead - it bakes a separate atlas
// per *requested* pixel size, on demand - so every label is rendered at the size it asks for
// and the only filtering left is the one pixel of anti-aliasing freetype put there.
//
// The faces are loaded once (the first is the primary, the rest are fallbacks for codepoints it
// does not carry, which is how the Chinese labels get glyphs from a system font), with light
// hinting: full hinting distorts stems at small sizes, and no hinting lets them wobble.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Rendering/Overlay/Im2D.h"

namespace Rendering::Overlay::Im2D
{
	/** One rasterized glyph, in pixels at its atlas' size. */
	struct Glyph
	{
		uint32_t codepoint = 0;
		float advanceX = 0.0f;
		/** Offset from the pen position (on the baseline) to the top-left of the quad; y is
		 * already flipped to point down, which is the screen's direction. */
		float offsetX = 0.0f;
		float offsetY = 0.0f;
		float width = 0.0f;
		float height = 0.0f;
		/** (u0, v0, u1, v1) in the atlas. */
		float u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f;
		/** Which of the font's atlases carries this glyph (the texture to sample). */
		int atlasIndex = 0;
	};

	/** A font: one or more faces, a glyph cache, and one atlas per pixel size that was asked
	 * for. */
	class Font
	{
	public:
		Font();
		~Font();
		Font(const Font&) = delete;
		Font& operator=(const Font&) = delete;

		/** Loads the faces. p_paths[0] is the primary face; later ones are consulted per
		 * codepoint, which is how Chinese labels get their glyphs when the primary face is a
		 * Latin one. p_referenceSize only seeds the ascent/line height reported before a size
		 * is used; every drawn size gets its own atlas. */
		bool Load(const std::vector<std::string>& p_paths, float p_referenceSize = 24.0f);
		bool IsLoaded() const;

		/** Returns the glyph for a codepoint at p_size pixels, rasterizing it into that size's
		 * atlas on first use. Returns nullptr when no face carries it, or the atlas is full. */
		const Glyph* FindGlyph(uint32_t p_codepoint, float p_size);

		/** The size the metrics below (ascent/descent/line height) are reported at. */
		float GetAscent(float p_size);
		float GetDescent(float p_size);
		float GetLineHeight(float p_size);

		// -- atlases -----------------------------------------------------------------------
		/** One atlas per distinct pixel size, in creation order. The renderer uploads the ones
		 * whose ConsumeAtlasDirty() reports true and binds the matching texture per draw. */
		int GetAtlasCount() const;
		int GetAtlasWidth(int p_atlasIndex) const;
		int GetAtlasHeight(int p_atlasIndex) const;
		float GetAtlasSize(int p_atlasIndex) const;
		/** RGBA8, top-down rows, white with the coverage in alpha. */
		const std::vector<uint8_t>& GetAtlasPixels(int p_atlasIndex) const;
		bool ConsumeAtlasDirty(int p_atlasIndex);
		TextureId GetTextureId(int p_atlasIndex) const;
		void SetTextureId(int p_atlasIndex, TextureId p_texture);

	private:
		struct Impl;
		std::unique_ptr<Impl> m_impl;
	};
}
