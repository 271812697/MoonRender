#include <Rendering/Overlay/ViewCubeLabels.h>

#include <utility>

namespace
{
	/** The builder lives in a function-local static on purpose: the editor installs it from a
	 * static initializer of its own translation unit, and a namespace-scope std::function here
	 * could be constructed *after* that registration (static initialization order across
	 * translation units is unspecified), wiping the callback and leaving the view cube without its
	 * labels. Constructing on first use removes the ordering problem: whichever call comes first
	 * builds the object, the other one sees the registered value. */
	Rendering::Overlay::ViewCubeLabels::AtlasBuilder& Builder()
	{
		static Rendering::Overlay::ViewCubeLabels::AtlasBuilder builder;
		return builder;
	}

	bool& Dirty()
	{
		static bool dirty = true;
		return dirty;
	}
}

void Rendering::Overlay::ViewCubeLabels::FaceUvRect(int p_face, float& p_x, float& p_y, float& p_w, float& p_h)
{
	// One cell per face, inset by the same margin the labels are drawn with, so the text sits in
	// the middle of the rectangle the face samples.
	const float inset = 0.08f;
	const float cw = 1.0f / 3.0f;
	const float ch = 0.5f;
	const int face = (p_face < 0 || p_face > 5) ? 0 : p_face;
	// Front, down, back, up, right, left - the atlas rows are (Left,Down,Back)/(Right,Up,Front)
	// in image space, and the cube's v axis points up, hence the row flip.
	const int colOf[6] = { 2, 1, 2, 1, 0, 0 };
	const int rowOf[6] = { 1, 0, 0, 1, 1, 0 };
	const int col = colOf[face];
	const int row = rowOf[face];
	p_x = col * cw + cw * inset;
	p_y = (1.0f - (row + 1) * ch) + ch * inset;
	p_w = cw * (1.0f - 2.0f * inset);
	p_h = ch * (1.0f - 2.0f * inset);
}

void Rendering::Overlay::ViewCubeLabels::SetAtlasBuilder(AtlasBuilder p_builder)
{
	Builder() = std::move(p_builder);
}

bool Rendering::Overlay::ViewCubeLabels::BuildAtlas(
	int p_cellPixels, std::vector<uint8_t>& p_rgba, uint32_t& p_width, uint32_t& p_height)
{
	p_rgba.clear();
	p_width = 0;
	p_height = 0;
	AtlasBuilder& builder = Builder();
	if (!builder)
	{
		return false;
	}

	builder(p_cellPixels, p_rgba, p_width, p_height);
	return !p_rgba.empty() && p_width > 0 && p_height > 0;
}

void Rendering::Overlay::ViewCubeLabels::Invalidate()
{
	Dirty() = true;
}

bool Rendering::Overlay::ViewCubeLabels::IsDirty()
{
	return Dirty();
}

void Rendering::Overlay::ViewCubeLabels::MarkClean()
{
	Dirty() = false;
}