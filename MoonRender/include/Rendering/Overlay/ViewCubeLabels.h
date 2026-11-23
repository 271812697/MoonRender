#pragma once
#include <cstdint>
#include <functional>
#include <vector>

namespace Rendering::Overlay::ViewCubeLabels
{
	/** Where a face's picture sits inside the atlas, in UV space.
	 *
	 * Pure data: the overlay geometry splits the cube's faces along these rectangles, so the picture
	 * and what the geometry samples cannot drift apart. It used to live next to the painter that
	 * draws the labels; the numbers belong to the engine, the painting does not. */
	void FaceUvRect(int p_face, float& p_x, float& p_y, float& p_w, float& p_h);

	/** Builds the label atlas: RGBA8 pixels, top-left origin, mipmapped from there.
	 *
	 * The engine cannot paint text - that needs the editor's font stack - so the editor installs the
	 * builder and the engine asks for the pixels when the labels are dirty. Filament keeps the same
	 * split for its font atlas: the engine owns the texture, the host owns the glyphs. */
	using AtlasBuilder = std::function<void(
		int p_cellPixels,
		std::vector<uint8_t>& p_rgba,
		uint32_t& p_width,
		uint32_t& p_height)>;

	void SetAtlasBuilder(AtlasBuilder p_builder);

	/** The atlas's dirty flag lives here too: the overlay is what consumes it (it re-uploads when
	 * the labels change), and it is the engine that owns the texture. The editor marks it dirty
	 * when the language or theme changes. */
	void Invalidate();
	bool IsDirty();
	void MarkClean();

	/** Runs the installed builder. False when none is installed yet, or the atlas came back empty. */
	bool BuildAtlas(int p_cellPixels, std::vector<uint8_t>& p_rgba, uint32_t& p_width, uint32_t& p_height);
}
