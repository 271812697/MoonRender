#pragma once

// Im2D: a standalone 2D drawing library - ImGui's draw list without ImGui.
//
// The editor's overlays (sketch annotations, dimension widgets, the view cube) are drawn in
// screen space. They used to be built by ImGui's ImDrawList and ImGui's own renderer, which
// dragged the whole windowing layer (context, io, fonts, draw data) into every call site and
// kept the overlay geometry on the UI thread's data structures. This is that drawing layer on
// its own: a vertex/index/command buffer with the same primitives ImGui offers, its own font
// atlas (freetype), and a GL submission in Im2DRender. ImGui stays for debugging only.
//
// The API deliberately mirrors ImDrawList where it can, so call sites migrate by changing the
// type of the draw list and nothing else. Colours are ImGui's encoding: 0xAABBGGRR, i.e. the
// four bytes in memory are R, G, B, A, which is what the GL vertex format reads.

#include <cstdint>
#include <string>
#include <vector>

namespace Rendering::Overlay::Im2D
{
	using TextureId = uint64_t;   ///< GL texture name; 0 means "no texture" (solid white).
	using ImU32 = uint32_t;

	// ---------------------------------------------------------------------------------------
	// Colours (ImGui's encoding: R in the low byte, A in the high byte)
	// ---------------------------------------------------------------------------------------
	/** Bytes, 0..255. The parameters are ints, not uint8_t, so that a call with integer
	 * literals is not ambiguous against the float overload below. */
	constexpr ImU32 ColorU32(int p_r, int p_g, int p_b, int p_a = 255)
	{
		return (static_cast<ImU32>(p_a & 0xFF) << 24) | (static_cast<ImU32>(p_b & 0xFF) << 16)
			| (static_cast<ImU32>(p_g & 0xFF) << 8) | static_cast<ImU32>(p_r & 0xFF);
	}
	constexpr ImU32 ColorU32(float p_r, float p_g, float p_b, float p_a = 1.0f)
	{
		return ColorU32(
			static_cast<uint8_t>(p_r * 255.0f + 0.5f),
			static_cast<uint8_t>(p_g * 255.0f + 0.5f),
			static_cast<uint8_t>(p_b * 255.0f + 0.5f),
			static_cast<uint8_t>(p_a * 255.0f + 0.5f));
	}
	constexpr ImU32 WithAlpha(ImU32 p_color, float p_alpha)
	{
		const auto a = static_cast<uint32_t>(static_cast<float>((p_color >> 24) & 0xFF) * p_alpha + 0.5f);
		return (p_color & 0x00FFFFFFu) | ((a > 255u ? 255u : a) << 24);
	}

	// ---------------------------------------------------------------------------------------
	// Geometry
	// ---------------------------------------------------------------------------------------
	struct Vec2
	{
		float x = 0.0f;
		float y = 0.0f;

		constexpr Vec2() = default;
		constexpr Vec2(float p_x, float p_y) : x(p_x), y(p_y) {}

		Vec2 operator+(const Vec2& p_other) const { return { x + p_other.x, y + p_other.y }; }
		Vec2 operator-(const Vec2& p_other) const { return { x - p_other.x, y - p_other.y }; }
		Vec2 operator*(float p_scale) const { return { x * p_scale, y * p_scale }; }

		/** Anything with x/y members converts implicitly - ImGui's ImVec2 in particular, while
		 * ImGui is still around for the editor's input and debug overlays. The two are layout
		 * compatible (two floats), so this is a plain copy. */
		template<class T, class = decltype(T::x)>
		Vec2(const T& p_other) : x(p_other.x), y(p_other.y) {}
	};

	struct Vec4
	{
		float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;
	};

	/** One vertex: the position in screen pixels, the texture coordinate and the colour. The
	 * layout matches the GL vertex format (2f + 2f + 4ub), so a draw list buffer can be handed
	 * to glBufferData as it is. */
	struct DrawVert
	{
		Vec2 pos;
		Vec2 uv;
		ImU32 col = 0;
	};

	enum class DrawFlags : uint32_t
	{
		None = 0,
		Closed = 1u << 0,      ///< PathStroke: close the path before stroking it.
		RoundCornersNone = 1u << 8,   ///< Force square corners even when a rounding is given.
		RoundCornersAll = 0xF0,
		RoundCornersTopLeft = 1u << 4,
		RoundCornersTopRight = 1u << 5,
		RoundCornersBottomLeft = 1u << 6,
		RoundCornersBottomRight = 1u << 7,
	};
	inline DrawFlags operator|(DrawFlags p_a, DrawFlags p_b)
	{
		return static_cast<DrawFlags>(static_cast<uint32_t>(p_a) | static_cast<uint32_t>(p_b));
	}
	inline bool HasFlag(DrawFlags p_flags, DrawFlags p_bit)
	{
		return (static_cast<uint32_t>(p_flags) & static_cast<uint32_t>(p_bit)) != 0;
	}

	/** A run of indices that share a texture and a clip rectangle. */
	struct DrawCmd
	{
		TextureId textureId = 0;
		uint32_t indexOffset = 0;
		uint32_t elementCount = 0;
		uint32_t vertexOffset = 0;
		/** Scissor rectangle in pixels (x0, y0, x1, y1), already clipped by the parents. */
		Vec4 clipRect;
	};

	class Font;

	// ---------------------------------------------------------------------------------------
	// DrawList
	// ---------------------------------------------------------------------------------------
	/** The geometry of one frame, in screen pixels. Mirrors ImDrawList's primitives; see the
	 * note at the top of the file for why it exists. */
	class DrawList
	{
	public:
		explicit DrawList(float p_curveTessellationTolerance = 1.25f);

		// -- frame ---------------------------------------------------------------------------
		void Reset();
		void PushClipRect(const Vec2& p_min, const Vec2& p_max);
		void PopClipRect();
		void PushTexture(TextureId p_texture);
		void PopTexture();

		// -- primitives ----------------------------------------------------------------------
		void AddLine(const Vec2& p_a, const Vec2& p_b, ImU32 p_col, float p_thickness = 1.0f);
		void AddLineH(float p_minX, float p_maxX, float p_y, ImU32 p_col, float p_thickness = 1.0f);
		void AddLineV(float p_x, float p_minY, float p_maxY, ImU32 p_col, float p_thickness = 1.0f);
		void AddRect(const Vec2& p_min, const Vec2& p_max, ImU32 p_col, float p_rounding = 0.0f,
			float p_thickness = 1.0f, DrawFlags p_flags = DrawFlags::RoundCornersAll);
		void AddRectFilled(const Vec2& p_min, const Vec2& p_max, ImU32 p_col, float p_rounding = 0.0f,
			DrawFlags p_flags = DrawFlags::RoundCornersAll);
		/** A rectangle whose four corners carry their own colour (a gradient fill). */
		void AddRectFilledMultiColor(const Vec2& p_min, const Vec2& p_max, ImU32 p_colUpLeft,
			ImU32 p_colUpRight, ImU32 p_colBotRight, ImU32 p_colBotLeft);
		void AddCircle(const Vec2& p_center, float p_radius, ImU32 p_col, int p_segments = 0,
			float p_thickness = 1.0f);
		void AddCircleFilled(const Vec2& p_center, float p_radius, ImU32 p_col, int p_segments = 0);
		/** A regular polygon with an explicit corner count. */
		void AddNgon(const Vec2& p_center, float p_radius, ImU32 p_col, int p_segments, float p_thickness = 1.0f);
		void AddNgonFilled(const Vec2& p_center, float p_radius, ImU32 p_col, int p_segments);
		/** An ellipse: two radii and a rotation of the radius axes, in radians. */
		void AddEllipse(const Vec2& p_center, const Vec2& p_radius, ImU32 p_col, float p_rotation = 0.0f,
			int p_segments = 0, float p_thickness = 1.0f);
		void AddEllipseFilled(const Vec2& p_center, const Vec2& p_radius, ImU32 p_col,
			float p_rotation = 0.0f, int p_segments = 0);
		void AddTriangle(const Vec2& p_a, const Vec2& p_b, const Vec2& p_c, ImU32 p_col, float p_thickness = 1.0f);
		void AddTriangleFilled(const Vec2& p_a, const Vec2& p_b, const Vec2& p_c, ImU32 p_col);
		void AddQuad(const Vec2& p_a, const Vec2& p_b, const Vec2& p_c, const Vec2& p_d, ImU32 p_col,
			float p_thickness = 1.0f);
		void AddQuadFilled(const Vec2& p_a, const Vec2& p_b, const Vec2& p_c, const Vec2& p_d, ImU32 p_col);
		void AddPolyline(const Vec2* p_points, int p_count, ImU32 p_col, float p_thickness, DrawFlags p_flags = DrawFlags::None);
		/** Fills a polygon. The polygon must be convex or the result is wrong; use
		 * AddConcavePolyFilled for a general simple polygon. */
		void AddConvexPolyFilled(const Vec2* p_points, int p_count, ImU32 p_col);
		/** Fills any simple (non self-intersecting) polygon, triangulating it first. */
		void AddConcavePolyFilled(const Vec2* p_points, int p_count, ImU32 p_col);
		/** Bezier curves, sampled into a polyline and stroked. */
		void AddBezierCubic(const Vec2& p_p1, const Vec2& p_p2, const Vec2& p_p3, const Vec2& p_p4, ImU32 p_col,
			float p_thickness, int p_segments = 0);
		void AddBezierQuadratic(const Vec2& p_p1, const Vec2& p_p2, const Vec2& p_p3, ImU32 p_col,
			float p_thickness, int p_segments = 0);

		// -- textures ------------------------------------------------------------------------
		void AddImage(TextureId p_texture, const Vec2& p_min, const Vec2& p_max, ImU32 p_tint = 0xFFFFFFFFu,
			const Vec2& p_uvMin = {}, const Vec2& p_uvMax = { 1.0f, 1.0f });
		/** A texture mapped onto an arbitrary quadrilateral - the four corners are given
		 * separately, so a rectangle in the sketch plane projects to a trapezoid here. */
		void AddImageQuad(TextureId p_texture, const Vec2& p_p1, const Vec2& p_p2, const Vec2& p_p3, const Vec2& p_p4,
			ImU32 p_tint = 0xFFFFFFFFu, const Vec2& p_uv1 = {}, const Vec2& p_uv2 = { 1.0f, 0.0f },
			const Vec2& p_uv3 = { 1.0f, 1.0f }, const Vec2& p_uv4 = { 0.0f, 1.0f });
		/** A textured rectangle with rounded corners (the texture is not distorted: the UVs stay
		 * affine over the rectangle, and the rounded silhouette comes from the tessellation). */
		void AddImageRounded(TextureId p_texture, const Vec2& p_min, const Vec2& p_max, ImU32 p_tint,
			float p_rounding, DrawFlags p_flags = DrawFlags::RoundCornersAll,
			const Vec2& p_uvMin = {}, const Vec2& p_uvMax = { 1.0f, 1.0f });

		// -- text ----------------------------------------------------------------------------
		/** Draws UTF-8 text at p_pos (top-left of the first line) and returns the position below
		 * the last line. The font's glyphs are baked on demand into its atlas, which the renderer
		 * uploads before drawing; see Im2DFont.cpp. */
		Vec2 AddText(Font& p_font, float p_size, const Vec2& p_pos, ImU32 p_col, const char* p_textBegin,
			const char* p_textEnd = nullptr);
		/** p_wrapWidth > 0 wraps the text to that many pixels. */
		Vec2 AddText(Font& p_font, float p_size, const Vec2& p_pos, ImU32 p_col, const char* p_textBegin,
			const char* p_textEnd, float p_wrapWidth);
		Vec2 AddText(Font& p_font, float p_size, float p_uiScale, const Vec2& p_pos, ImU32 p_col,
			const char* p_textBegin, const char* p_textEnd = nullptr, float p_wrapWidth = 0.0f);
		static Vec2 CalcTextSize(Font& p_font, float p_size, const char* p_textBegin, const char* p_textEnd = nullptr);

		// -- path ----------------------------------------------------------------------------
		void PathClear();
		void PathLineTo(const Vec2& p_pos);
		void PathArcTo(const Vec2& p_center, float p_radius, float p_angleMin, float p_angleMax, int p_segments = 0);
		/** Same, but the angles are given in twelfths of a turn (a cheap circle). */
		void PathArcToFast(const Vec2& p_center, float p_radius, int p_angleMin12, int p_angleMax12);
		void PathEllipticalArcTo(const Vec2& p_center, const Vec2& p_radius, float p_rotation, float p_angleMin,
			float p_angleMax, int p_segments = 0);
		void PathBezierCubicCurveTo(const Vec2& p_p2, const Vec2& p_p3, const Vec2& p_p4, int p_segments = 0);
		void PathBezierQuadraticCurveTo(const Vec2& p_p2, const Vec2& p_p3, int p_segments = 0);
		void PathRect(const Vec2& p_min, const Vec2& p_max, float p_rounding = 0.0f, DrawFlags p_flags = DrawFlags::RoundCornersAll);
		void PathFillConvex(ImU32 p_col);
		void PathStroke(ImU32 p_col, float p_thickness = 1.0f, DrawFlags p_flags = DrawFlags::None);

		/** Gives back the tail of the last PrimReserve: used when a primitive was reserved for
		 * its worst case and turned out shorter (the text path does this for skipped glyphs). */
		void PrimUnreserve(int p_indexCount, int p_vertexCount);

		// -- output --------------------------------------------------------------------------
		void AddDrawCommand(TextureId p_textureId = 0);
		const std::vector<DrawVert>& GetVertices() const { return m_vertices; }
		const std::vector<uint32_t>& GetIndices() const { return m_indices; }
		const std::vector<DrawCmd>& GetCommands() const { return m_commands; }
		bool IsEmpty() const { return m_commands.empty(); }

	private:
		void PrimReserve(int p_indexCount, int p_vertexCount);
		void PrimWriteVertex(const Vec2& p_pos, const Vec2& p_uv, ImU32 p_col);
		void PrimWriteIndex(uint32_t p_index);
		void PrimRect(const Vec2& p_a, const Vec2& p_b, ImU32 p_col);
		void PrimQuadUV(const Vec2& p_a, const Vec2& p_b, const Vec2& p_c, const Vec2& p_d,
			const Vec2& p_uvA, const Vec2& p_uvB, const Vec2& p_uvC, const Vec2& p_uvD, ImU32 p_col);
		/** A quad whose four corners carry their own colours (used by the anti-aliased fringes
		 * and gradients). */
		void PrimQuadUVColor(const Vec2& p_a, const Vec2& p_b, const Vec2& p_c, const Vec2& p_d,
			const Vec2& p_uvA, const Vec2& p_uvB, const Vec2& p_uvC, const Vec2& p_uvD,
			ImU32 p_colA, ImU32 p_colB, ImU32 p_colC, ImU32 p_colD);
		int PathSegmentCount(float p_radius) const;

		std::vector<DrawVert> m_vertices;
		std::vector<uint32_t> m_indices;
		std::vector<DrawCmd> m_commands;
		std::vector<Vec2> m_path;

		TextureId m_currentTexture = 0;
		Vec4 m_currentClipRect{ 0.0f, 0.0f, 8192.0f, 8192.0f };
		std::vector<Vec4> m_clipStack;
		std::vector<TextureId> m_textureStack;
		float m_tessellationTolerance = 1.25f;
		/** Set while a primitive is being written: the start of the run the current command
		 * covers, so AddDrawCommand can turn it into a DrawCmd. */
		uint32_t m_commandIndexStart = 0;
		uint32_t m_commandVertexStart = 0;
		/** The reservation of the primitive being written (see PrimReserve). */
		uint32_t m_primVtxStart = 0;
		uint32_t m_primIdxStart = 0;
		uint32_t m_primVtxCursor = 0;
		uint32_t m_primIdxCursor = 0;
	};

	// ---------------------------------------------------------------------------------------
	// Higher level shapes
	// ---------------------------------------------------------------------------------------
	/** A position plus an angle, in degrees, measured in screen space (x right, y down) from the
	 * positive x axis. Matches the Transform the dimension drawing used with ImGui. */
	struct Transform
	{
		Vec2 pos;
		float angleDeg = 0.0f;

		Transform() = default;
		Transform(float p_x, float p_y, float p_angleDeg) : pos(p_x, p_y), angleDeg(p_angleDeg) {}
		Transform(const Vec2& p_pos, float p_angleDeg) : pos(p_pos), angleDeg(p_angleDeg) {}
	};

	/** A dimension arrow: a shaft from the transform's position plus a filled head whose tip is
	 * at the end of the length. */
	void AddArrow(DrawList& p_list, const Transform& p_transform, ImU32 p_col, float p_length, float p_thickness);
	/** A dimension line with a head at either end, both tips on the shot's endpoints. */
	void AddDoubleArrow(DrawList& p_list, const Transform& p_transform, ImU32 p_col, float p_length, float p_thickness);
	/** The same, with the value text above the shaft (the label the dimension previews show). */
	void AddDoubleArrow(DrawList& p_list, Font& p_font, float p_fontSize, const Transform& p_transform, ImU32 p_col,
		float p_length, float p_thickness, const char* p_text);
	/** A dimension arc from the transform's angle, swept by p_degree degrees, with a head at
	 * either end (the tips sit on the arc's endpoints). */
	void AddDoubleArcArrow(DrawList& p_list, const Transform& p_transform, ImU32 p_col, float p_radius,
		float p_degree, float p_thickness);
	/** The same, with the value text on the outer side of the arc. */
	void AddDoubleArcArrow(DrawList& p_list, Font& p_font, float p_fontSize, const Transform& p_transform,
		ImU32 p_col, float p_radius, float p_degree, float p_thickness, const char* p_text);
	/** Text along a rotated baseline - what a dimension label needs once it has to follow the
	 * sketch plane instead of staying screen aligned. */
	void AddTextRotated(DrawList& p_list, Font& p_font, float p_size, const Transform& p_transform, ImU32 p_col,
		const char* p_text);
}
