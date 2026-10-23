#include "Rendering/Overlay/Im2D.h"

#include "Rendering/Overlay/Im2DFont.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>

// ImDrawList's primitives, reimplemented without ImGui.
//
// The algorithms are ImGui's (imgui_draw.cpp, MIT): the anti-aliased fringe around filled
// polygons and thick lines is what makes screen-space overlay geometry look like it belongs to
// the same renderer as the rest. Two simplifications are deliberate and documented where they
// happen: joins/caps of a thick polyline are drawn as small filled circles instead of ImGui's
// exact miter/bevel construction, and concave polygons are not triangulated (the CAD overlays
// only fill convex shapes).

namespace Rendering::Overlay::Im2D
{
	uint32_t Im2DDecodeUtf8(const char*& p_text, const char* p_textEnd);

	namespace
	{
		constexpr float kPi = 3.14159265358979f;
		constexpr float kAASize = 1.0f;          // one pixel of anti-aliasing fringe
		constexpr ImU32 kWhiteUv = 0;            // the renderer binds a 1x1 white texture for id 0

		inline ImU32 ColorTransparent(ImU32 p_col) { return p_col & ~0xFF000000u; }

		inline Vec2 Rotate(const Vec2& p_v, float p_sin, float p_cos)
		{
			return { p_v.x * p_cos - p_v.y * p_sin, p_v.x * p_sin + p_v.y * p_cos };
		}
		inline float Length(const Vec2& p_v) { return std::sqrt(p_v.x * p_v.x + p_v.y * p_v.y); }
	}

	DrawList::DrawList(float p_curveTessellationTolerance)
		: m_tessellationTolerance(p_curveTessellationTolerance)
	{
		Reset();
	}

	int DrawList::PathSegmentCount(float p_radius) const
	{
		if (p_radius <= 0.0f)
		{
			return 0;
		}
		// Enough segments that the chord error stays under the tessellation tolerance.
		const float ratio = 1.0f - std::min(1.0f, m_tessellationTolerance / p_radius);
		float segments = 2.0f * kPi / std::acos(std::max(-1.0f, std::min(1.0f, ratio)));
		segments = std::clamp(segments, 8.0f, 128.0f);
		return static_cast<int>(segments);
	}

	// ---------------------------------------------------------------------------------------
	// Frame / state
	// ---------------------------------------------------------------------------------------
	void DrawList::Reset()
	{
		m_vertices.clear();
		m_indices.clear();
		m_commands.clear();
		m_path.clear();
		m_clipStack.clear();
		m_textureStack.clear();
		m_currentTexture = 0;
		m_currentClipRect = Vec4{ -8192.0f, -8192.0f, 8192.0f, 8192.0f };
		m_commandIndexStart = 0;
		m_commandVertexStart = 0;
	}

	void DrawList::AddDrawCommand(TextureId p_textureId)
	{
		const uint32_t indexCount = static_cast<uint32_t>(m_indices.size()) - m_commandIndexStart;
		if (indexCount == 0)
		{
			return;
		}
		DrawCmd cmd;
		cmd.textureId = p_textureId != 0 ? p_textureId : m_currentTexture;
		cmd.indexOffset = m_commandIndexStart;
		cmd.elementCount = indexCount;
		cmd.vertexOffset = m_commandVertexStart;
		cmd.clipRect = m_currentClipRect;
		m_commands.push_back(cmd);

		m_commandIndexStart = static_cast<uint32_t>(m_indices.size());
		m_commandVertexStart = static_cast<uint32_t>(m_vertices.size());
	}

	void DrawList::PushClipRect(const Vec2& p_min, const Vec2& p_max)
	{
		AddDrawCommand();
		m_clipStack.push_back(m_currentClipRect);
		m_currentClipRect.x = std::max(m_currentClipRect.x, p_min.x);
		m_currentClipRect.y = std::max(m_currentClipRect.y, p_min.y);
		m_currentClipRect.z = std::min(m_currentClipRect.z, p_max.x);
		m_currentClipRect.w = std::min(m_currentClipRect.w, p_max.y);
	}

	void DrawList::PopClipRect()
	{
		AddDrawCommand();
		if (!m_clipStack.empty())
		{
			m_currentClipRect = m_clipStack.back();
			m_clipStack.pop_back();
		}
	}

	void DrawList::PushTexture(TextureId p_texture)
	{
		AddDrawCommand();
		m_textureStack.push_back(m_currentTexture);
		m_currentTexture = p_texture;
	}

	void DrawList::PopTexture()
	{
		AddDrawCommand();
		if (!m_textureStack.empty())
		{
			m_currentTexture = m_textureStack.back();
			m_textureStack.pop_back();
		}
	}

	// ---------------------------------------------------------------------------------------
	// Primitive writing
	// ---------------------------------------------------------------------------------------
	void DrawList::PrimReserve(int p_indexCount, int p_vertexCount)
	{
		m_primVtxStart = static_cast<uint32_t>(m_vertices.size());
		m_primIdxStart = static_cast<uint32_t>(m_indices.size());
		m_primVtxCursor = m_primVtxStart;
		m_primIdxCursor = m_primIdxStart;
		m_vertices.resize(m_vertices.size() + static_cast<size_t>(p_vertexCount));
		m_indices.resize(m_indices.size() + static_cast<size_t>(p_indexCount));
	}

	void DrawList::PrimWriteVertex(const Vec2& p_pos, const Vec2& p_uv, ImU32 p_col)
	{
		assert(m_primVtxCursor < m_vertices.size());
		m_vertices[m_primVtxCursor].pos = p_pos;
		m_vertices[m_primVtxCursor].uv = p_uv;
		m_vertices[m_primVtxCursor].col = p_col;
		++m_primVtxCursor;
	}

	void DrawList::PrimWriteIndex(uint32_t p_index)
	{
		assert(m_primIdxCursor < m_indices.size());
		m_indices[m_primIdxCursor++] = p_index;
	}

	void DrawList::PrimRect(const Vec2& p_a, const Vec2& p_b, ImU32 p_col)
	{
		const Vec2 uv(kWhiteUv, kWhiteUv);
		PrimReserve(6, 4);
		PrimWriteVertex(Vec2(p_a.x, p_a.y), uv, p_col);
		PrimWriteVertex(Vec2(p_b.x, p_a.y), uv, p_col);
		PrimWriteVertex(Vec2(p_b.x, p_b.y), uv, p_col);
		PrimWriteVertex(Vec2(p_a.x, p_b.y), uv, p_col);
		PrimWriteIndex(m_primVtxStart);
		PrimWriteIndex(m_primVtxStart + 1);
		PrimWriteIndex(m_primVtxStart + 2);
		PrimWriteIndex(m_primVtxStart);
		PrimWriteIndex(m_primVtxStart + 2);
		PrimWriteIndex(m_primVtxStart + 3);
	}

	void DrawList::PrimQuadUV(const Vec2& p_a, const Vec2& p_b, const Vec2& p_c, const Vec2& p_d,
		const Vec2& p_uvA, const Vec2& p_uvB, const Vec2& p_uvC, const Vec2& p_uvD, ImU32 p_col)
	{
		PrimReserve(6, 4);
		PrimWriteVertex(p_a, p_uvA, p_col);
		PrimWriteVertex(p_b, p_uvB, p_col);
		PrimWriteVertex(p_c, p_uvC, p_col);
		PrimWriteVertex(p_d, p_uvD, p_col);
		PrimWriteIndex(m_primVtxStart);
		PrimWriteIndex(m_primVtxStart + 1);
		PrimWriteIndex(m_primVtxStart + 2);
		PrimWriteIndex(m_primVtxStart);
		PrimWriteIndex(m_primVtxStart + 2);
		PrimWriteIndex(m_primVtxStart + 3);
	}

	void DrawList::PrimQuadUVColor(const Vec2& p_a, const Vec2& p_b, const Vec2& p_c, const Vec2& p_d,
		const Vec2& p_uvA, const Vec2& p_uvB, const Vec2& p_uvC, const Vec2& p_uvD,
		ImU32 p_colA, ImU32 p_colB, ImU32 p_colC, ImU32 p_colD)
	{
		PrimReserve(6, 4);
		PrimWriteVertex(p_a, p_uvA, p_colA);
		PrimWriteVertex(p_b, p_uvB, p_colB);
		PrimWriteVertex(p_c, p_uvC, p_colC);
		PrimWriteVertex(p_d, p_uvD, p_colD);
		PrimWriteIndex(m_primVtxStart);
		PrimWriteIndex(m_primVtxStart + 1);
		PrimWriteIndex(m_primVtxStart + 2);
		PrimWriteIndex(m_primVtxStart);
		PrimWriteIndex(m_primVtxStart + 2);
		PrimWriteIndex(m_primVtxStart + 3);
	}

	// ---------------------------------------------------------------------------------------
	// Filled shapes
	// ---------------------------------------------------------------------------------------
	namespace
	{
		/** Ear clipping for a simple (non self-intersecting) polygon. Returns triples of indices
		 * into p_points; an empty result means the polygon was degenerate.
		 *
		 * This is ImGui's ImTriangulator in its simplest form: no holes, no self-intersection
		 * handling, and a fan as the way out of a polygon it cannot clip (a valid result for the
		 * shapes an overlay draws, and it keeps the loop finite).
		 */
		std::vector<int> TriangulateSimplePolygon(const Vec2* p_points, int p_count)
		{
			std::vector<int> triangles;
			if (p_count < 3)
			{
				return triangles;
			}

			float area = 0.0f;
			for (int i = 0; i < p_count; ++i)
			{
				const Vec2& a = p_points[i];
				const Vec2& b = p_points[(i + 1) % p_count];
				area += a.x * b.y - b.x * a.y;
			}
			const bool counterClockwise = area >= 0.0f;

			std::vector<int> order(static_cast<size_t>(p_count));
			for (int i = 0; i < p_count; ++i)
			{
				order[static_cast<size_t>(i)] = i;
			}
			if (!counterClockwise)
			{
				std::reverse(order.begin(), order.end());
			}

			const auto cross = [](const Vec2& a, const Vec2& b, const Vec2& c) {
				return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
			};
			const auto insideTriangle = [&](const Vec2& a, const Vec2& b, const Vec2& c, const Vec2& p) {
				const float d0 = cross(a, b, p);
				const float d1 = cross(b, c, p);
				const float d2 = cross(c, a, p);
				return d0 >= 0.0f && d1 >= 0.0f && d2 >= 0.0f;
			};

			// Bounded: every pass removes at least one ear, or the fallback below fires.
			int guard = p_count * p_count + 8;
			while (order.size() >= 3 && guard-- > 0)
			{
				bool clipped = false;
				const int n = static_cast<int>(order.size());
				for (int i = 0; i < n; ++i)
				{
					const int i0 = order[(i + n - 1) % n];
					const int i1 = order[i];
					const int i2 = order[(i + 1) % n];
					const Vec2& a = p_points[i0];
					const Vec2& b = p_points[i1];
					const Vec2& c = p_points[i2];
					if (cross(a, b, c) <= 0.0f)
					{
						continue;  // reflex corner, not an ear
					}
					bool blocked = false;
					for (int k = 0; k < n && !blocked; ++k)
					{
						const int ik = order[k];
						if (ik == i0 || ik == i1 || ik == i2)
						{
							continue;
						}
						blocked = insideTriangle(a, b, c, p_points[ik]);
					}
					if (blocked)
					{
						continue;
					}
					triangles.push_back(i0);
					triangles.push_back(i1);
					triangles.push_back(i2);
					order.erase(order.begin() + i);
					clipped = true;
					break;
				}
				if (!clipped)
				{
					// No ear: a self-intersecting or otherwise degenerate outline. Fan it, so
					// something is drawn instead of nothing.
					for (size_t i = 1; i + 1 < order.size(); ++i)
					{
						triangles.push_back(order[0]);
						triangles.push_back(order[i]);
						triangles.push_back(order[i + 1]);
					}
					order.clear();
				}
			}
			return triangles;
		}
	}

	void DrawList::AddConvexPolyFilled(const Vec2* p_points, int p_count, ImU32 p_col)
	{
		if (p_count < 3)
		{
			return;
		}

		// Filled core (a triangle fan) plus a one pixel fringe that fades to transparent. The
		// fringe is what keeps the silhouette from stair-stepping; the winding decides which side
		// of each edge is outside.
		float area = 0.0f;
		for (int i = 0; i < p_count; ++i)
		{
			const Vec2& a = p_points[i];
			const Vec2& b = p_points[(i + 1) % p_count];
			area += a.x * b.y - b.x * a.y;
		}
		const float sign = area >= 0.0f ? 1.0f : -1.0f;

		const Vec2 uv(kWhiteUv, kWhiteUv);
		const ImU32 colTransparent = ColorTransparent(p_col);

		// Core: fan from the first vertex.
		PrimReserve((p_count - 2) * 3, p_count);
		for (int i = 0; i < p_count; ++i)
		{
			PrimWriteVertex(p_points[i], uv, p_col);
		}
		for (int i = 2; i < p_count; ++i)
		{
			PrimWriteIndex(m_primVtxStart);
			PrimWriteIndex(m_primVtxStart + i - 1);
			PrimWriteIndex(m_primVtxStart + i);
		}

		// Fringe: one quad per edge, inward opaque to outward transparent.
		PrimReserve(p_count * 6, p_count * 4);
		for (int i = 0; i < p_count; ++i)
		{
			const Vec2& a = p_points[i];
			const Vec2& b = p_points[(i + 1) % p_count];
			Vec2 edge = b - a;
			const float len = Length(edge);
			if (len <= 0.0001f)
			{
				continue;
			}
			Vec2 normal(-edge.y / len, edge.x / len);
			normal = normal * (kAASize * sign);
			PrimWriteVertex(a, uv, p_col);
			PrimWriteVertex(b, uv, p_col);
			PrimWriteVertex(b + normal, uv, colTransparent);
			PrimWriteVertex(a + normal, uv, colTransparent);
			const uint32_t base = m_primVtxCursor - 4;
			PrimWriteIndex(base);
			PrimWriteIndex(base + 1);
			PrimWriteIndex(base + 2);
			PrimWriteIndex(base);
			PrimWriteIndex(base + 2);
			PrimWriteIndex(base + 3);
		}
	}

	void DrawList::AddConcavePolyFilled(const Vec2* p_points, int p_count, ImU32 p_col)
	{
		if (p_count < 3)
		{
			return;
		}
		const std::vector<int> triangles = TriangulateSimplePolygon(p_points, p_count);
		if (triangles.empty())
		{
			return;
		}

		const Vec2 uv(kWhiteUv, kWhiteUv);
		const ImU32 colTransparent = ColorTransparent(p_col);

		// Core: the triangulation, with every triangle owning its three vertices (a fan's shared
		// vertices are only an optimisation, and sharing them here would need an index remap).
		const int triangleCount = static_cast<int>(triangles.size()) / 3;
		PrimReserve(triangleCount * 3, triangleCount * 3);
		for (int t = 0; t < triangleCount; ++t)
		{
			PrimWriteVertex(p_points[triangles[t * 3 + 0]], uv, p_col);
			PrimWriteVertex(p_points[triangles[t * 3 + 1]], uv, p_col);
			PrimWriteVertex(p_points[triangles[t * 3 + 2]], uv, p_col);
			PrimWriteIndex(m_primVtxStart + t * 3 + 0);
			PrimWriteIndex(m_primVtxStart + t * 3 + 1);
			PrimWriteIndex(m_primVtxStart + t * 3 + 2);
		}

		// Same fringe as the convex fill: it only depends on the outline's winding.
		float area = 0.0f;
		for (int i = 0; i < p_count; ++i)
		{
			const Vec2& a = p_points[i];
			const Vec2& b = p_points[(i + 1) % p_count];
			area += a.x * b.y - b.x * a.y;
		}
		const float sign = area >= 0.0f ? 1.0f : -1.0f;
		PrimReserve(p_count * 6, p_count * 4);
		for (int i = 0; i < p_count; ++i)
		{
			const Vec2& a = p_points[i];
			const Vec2& b = p_points[(i + 1) % p_count];
			Vec2 edge = b - a;
			const float len = Length(edge);
			if (len <= 0.0001f)
			{
				continue;
			}
			Vec2 normal(-edge.y / len, edge.x / len);
			normal = normal * (kAASize * sign);
			PrimWriteVertex(a, uv, p_col);
			PrimWriteVertex(b, uv, p_col);
			PrimWriteVertex(b + normal, uv, colTransparent);
			PrimWriteVertex(a + normal, uv, colTransparent);
			const uint32_t base = m_primVtxCursor - 4;
			PrimWriteIndex(base);
			PrimWriteIndex(base + 1);
			PrimWriteIndex(base + 2);
			PrimWriteIndex(base);
			PrimWriteIndex(base + 2);
			PrimWriteIndex(base + 3);
		}
	}

	void DrawList::AddTriangleFilled(const Vec2& p_a, const Vec2& p_b, const Vec2& p_c, ImU32 p_col)
	{
		const Vec2 points[3] = { p_a, p_b, p_c };
		AddConvexPolyFilled(points, 3, p_col);
	}

	void DrawList::AddQuadFilled(const Vec2& p_a, const Vec2& p_b, const Vec2& p_c, const Vec2& p_d, ImU32 p_col)
	{
		const Vec2 points[4] = { p_a, p_b, p_c, p_d };
		AddConvexPolyFilled(points, 4, p_col);
	}

	void DrawList::AddRectFilled(const Vec2& p_min, const Vec2& p_max, ImU32 p_col, float p_rounding, DrawFlags p_flags)
	{
		if ((p_col & 0xFF000000u) == 0)
		{
			return;
		}
		if (p_rounding <= 0.0f)
		{
			PrimRect(p_min, p_max, p_col);
			return;
		}
		PathRect(p_min, p_max, p_rounding, p_flags);
		PathFillConvex(p_col);
	}

	// ---------------------------------------------------------------------------------------
	// Stroked shapes
	// ---------------------------------------------------------------------------------------
	void DrawList::AddPolyline(const Vec2* p_points, int p_count, ImU32 p_col, float p_thickness, DrawFlags p_flags)
	{
		if (p_count < 2)
		{
			return;
		}
		const bool closed = HasFlag(p_flags, DrawFlags::Closed);
		const Vec2 uv(kWhiteUv, kWhiteUv);
		const ImU32 colTransparent = ColorTransparent(p_col);
		const float half = std::max(p_thickness, 1.0f) * 0.5f;
		const float fringe = kAASize * 0.5f;

		const int segmentCount = closed ? p_count : p_count - 1;
		for (int i = 0; i < segmentCount; ++i)
		{
			const Vec2& a = p_points[i];
			const Vec2& b = p_points[(i + 1) % p_count];
			Vec2 dir = b - a;
			const float len = Length(dir);
			if (len <= 0.0001f)
			{
				continue;
			}
			dir = Vec2(dir.x / len, dir.y / len);
			const Vec2 normal(-dir.y, dir.x);

			// Core quad.
			const Vec2 a0 = a + normal * half;
			const Vec2 a1 = a - normal * half;
			const Vec2 b0 = b + normal * half;
			const Vec2 b1 = b - normal * half;
			PrimReserve(6, 4);
			PrimWriteVertex(a0, uv, p_col);
			PrimWriteVertex(b0, uv, p_col);
			PrimWriteVertex(b1, uv, p_col);
			PrimWriteVertex(a1, uv, p_col);
			PrimWriteIndex(m_primVtxStart);
			PrimWriteIndex(m_primVtxStart + 1);
			PrimWriteIndex(m_primVtxStart + 2);
			PrimWriteIndex(m_primVtxStart);
			PrimWriteIndex(m_primVtxStart + 2);
			PrimWriteIndex(m_primVtxStart + 3);

			// Two one-pixel fringes, one per side.
			for (int side = 0; side < 2; ++side)
			{
				const float s = side == 0 ? 1.0f : -1.0f;
				const Vec2 outer0 = a + normal * (half + fringe) * s;
				const Vec2 outer1 = b + normal * (half + fringe) * s;
				const Vec2 inner0 = a + normal * half * s;
				const Vec2 inner1 = b + normal * half * s;
				PrimReserve(6, 4);
				PrimWriteVertex(inner0, uv, p_col);
				PrimWriteVertex(inner1, uv, p_col);
				PrimWriteVertex(outer1, uv, colTransparent);
				PrimWriteVertex(outer0, uv, colTransparent);
				PrimWriteIndex(m_primVtxStart);
				PrimWriteIndex(m_primVtxStart + 1);
				PrimWriteIndex(m_primVtxStart + 2);
				PrimWriteIndex(m_primVtxStart);
				PrimWriteIndex(m_primVtxStart + 2);
				PrimWriteIndex(m_primVtxStart + 3);
			}
		}

		// Joins and caps: a small disc at every shared point. ImGui builds an exact miter there;
		// a disc is visually indistinguishable at overlay thicknesses and much simpler, and it
		// also gives the caps a round profile.
		if (p_thickness > 1.0f)
		{
			const int firstDisc = 1;
			const int lastDisc = closed ? p_count : p_count - 1;
			for (int i = firstDisc; i < lastDisc; ++i)
			{
				AddCircleFilled(p_points[i], half, p_col, 8);
			}
			if (!closed)
			{
				AddCircleFilled(p_points[0], half, p_col, 8);
				AddCircleFilled(p_points[p_count - 1], half, p_col, 8);
			}
		}
	}

	void DrawList::AddLine(const Vec2& p_a, const Vec2& p_b, ImU32 p_col, float p_thickness)
	{
		if ((p_col & 0xFF000000u) == 0)
		{
			return;
		}
		const Vec2 points[2] = { p_a, p_b };
		AddPolyline(points, 2, p_col, p_thickness);
	}

	void DrawList::AddTriangle(const Vec2& p_a, const Vec2& p_b, const Vec2& p_c, ImU32 p_col, float p_thickness)
	{
		const Vec2 points[3] = { p_a, p_b, p_c };
		AddPolyline(points, 3, p_col, p_thickness, DrawFlags::Closed);
	}

	void DrawList::AddQuad(const Vec2& p_a, const Vec2& p_b, const Vec2& p_c, const Vec2& p_d, ImU32 p_col,
		float p_thickness)
	{
		const Vec2 points[4] = { p_a, p_b, p_c, p_d };
		AddPolyline(points, 4, p_col, p_thickness, DrawFlags::Closed);
	}

	void DrawList::AddRect(const Vec2& p_min, const Vec2& p_max, ImU32 p_col, float p_rounding, float p_thickness,
		DrawFlags p_flags)
	{
		PathRect(p_min, p_max, p_rounding, p_flags);
		PathStroke(p_col, p_thickness, DrawFlags::Closed);
	}

	void DrawList::AddCircle(const Vec2& p_center, float p_radius, ImU32 p_col, int p_segments, float p_thickness)
	{
		PathClear();
		PathArcTo(p_center, p_radius, 0.0f, 2.0f * kPi, p_segments);
		PathStroke(p_col, p_thickness, DrawFlags::Closed);
	}

	void DrawList::AddCircleFilled(const Vec2& p_center, float p_radius, ImU32 p_col, int p_segments)
	{
		PathClear();
		PathArcTo(p_center, p_radius, 0.0f, 2.0f * kPi, p_segments);
		PathFillConvex(p_col);
	}

	void DrawList::AddNgon(const Vec2& p_center, float p_radius, ImU32 p_col, int p_segments, float p_thickness)
	{
		if (p_segments < 3)
		{
			return;
		}
		PathClear();
		PathArcTo(p_center, p_radius, 0.0f, 2.0f * kPi, p_segments);
		PathStroke(p_col, p_thickness, DrawFlags::Closed);
	}

	void DrawList::AddNgonFilled(const Vec2& p_center, float p_radius, ImU32 p_col, int p_segments)
	{
		if (p_segments < 3)
		{
			return;
		}
		PathClear();
		PathArcTo(p_center, p_radius, 0.0f, 2.0f * kPi, p_segments);
		PathFillConvex(p_col);
	}

	void DrawList::AddEllipse(const Vec2& p_center, const Vec2& p_radius, ImU32 p_col, float p_rotation,
		int p_segments, float p_thickness)
	{
		PathClear();
		PathEllipticalArcTo(p_center, p_radius, p_rotation, 0.0f, 2.0f * kPi, p_segments);
		PathStroke(p_col, p_thickness, DrawFlags::Closed);
	}

	void DrawList::AddEllipseFilled(const Vec2& p_center, const Vec2& p_radius, ImU32 p_col, float p_rotation,
		int p_segments)
	{
		PathClear();
		PathEllipticalArcTo(p_center, p_radius, p_rotation, 0.0f, 2.0f * kPi, p_segments);
		PathFillConvex(p_col);
	}

	void DrawList::AddBezierCubic(const Vec2& p_p1, const Vec2& p_p2, const Vec2& p_p3, const Vec2& p_p4,
		ImU32 p_col, float p_thickness, int p_segments)
	{
		PathClear();
		PathLineTo(p_p1);
		PathBezierCubicCurveTo(p_p2, p_p3, p_p4, p_segments);
		PathStroke(p_col, p_thickness);
	}

	void DrawList::AddBezierQuadratic(const Vec2& p_p1, const Vec2& p_p2, const Vec2& p_p3, ImU32 p_col,
		float p_thickness, int p_segments)
	{
		PathClear();
		PathLineTo(p_p1);
		PathBezierQuadraticCurveTo(p_p2, p_p3, p_segments);
		PathStroke(p_col, p_thickness);
	}

	void DrawList::AddLineH(float p_minX, float p_maxX, float p_y, ImU32 p_col, float p_thickness)
	{
		AddLine(Vec2(p_minX, p_y), Vec2(p_maxX, p_y), p_col, p_thickness);
	}

	void DrawList::AddLineV(float p_x, float p_minY, float p_maxY, ImU32 p_col, float p_thickness)
	{
		AddLine(Vec2(p_x, p_minY), Vec2(p_x, p_maxY), p_col, p_thickness);
	}

	void DrawList::AddRectFilledMultiColor(const Vec2& p_min, const Vec2& p_max, ImU32 p_colUpLeft,
		ImU32 p_colUpRight, ImU32 p_colBotRight, ImU32 p_colBotLeft)
	{
		const Vec2 uv(kWhiteUv, kWhiteUv);
		PrimQuadUVColor(
			Vec2(p_min.x, p_min.y), Vec2(p_max.x, p_min.y), Vec2(p_max.x, p_max.y), Vec2(p_min.x, p_max.y),
			uv, uv, uv, uv,
			p_colUpLeft, p_colUpRight, p_colBotRight, p_colBotLeft);
	}

	// ---------------------------------------------------------------------------------------
	// Path
	// ---------------------------------------------------------------------------------------
	void DrawList::PathClear() { m_path.clear(); }

	void DrawList::PathLineTo(const Vec2& p_pos) { m_path.push_back(p_pos); }

	void DrawList::PathArcTo(const Vec2& p_center, float p_radius, float p_angleMin, float p_angleMax, int p_segments)
	{
		if (p_radius <= 0.0f)
		{
			return;
		}
		int segments = p_segments > 0 ? p_segments : PathSegmentCount(p_radius);
		const float step = (p_angleMax - p_angleMin) / static_cast<float>(segments);
		for (int i = 0; i <= segments; ++i)
		{
			const float angle = p_angleMin + step * static_cast<float>(i);
			m_path.push_back(Vec2(p_center.x + std::cos(angle) * p_radius, p_center.y + std::sin(angle) * p_radius));
		}
	}

	void DrawList::PathArcToFast(const Vec2& p_center, float p_radius, int p_angleMin12, int p_angleMax12)
	{
		if (p_radius <= 0.0f)
		{
			return;
		}
		if (p_angleMin12 < 0)
		{
			p_angleMin12 = 0;
		}
		if (p_angleMax12 > 12)
		{
			p_angleMax12 = 12;
		}
		if (p_angleMax12 <= p_angleMin12)
		{
			return;
		}
		// Every step is exactly 30 degrees, so the cheap path is just the arc with one segment
		// per twelfth.
		const float step = 2.0f * kPi / 12.0f;
		for (int i = p_angleMin12; i <= p_angleMax12; ++i)
		{
			const float angle = step * static_cast<float>(i);
			m_path.push_back(Vec2(p_center.x + std::cos(angle) * p_radius, p_center.y + std::sin(angle) * p_radius));
		}
	}

	void DrawList::PathEllipticalArcTo(const Vec2& p_center, const Vec2& p_radius, float p_rotation,
		float p_angleMin, float p_angleMax, int p_segments)
	{
		if (p_radius.x <= 0.0f || p_radius.y <= 0.0f)
		{
			return;
		}
		// Enough segments for the larger radius; the smaller one is only ever smoother.
		const int segments = p_segments > 0 ? p_segments
			: PathSegmentCount(std::max(p_radius.x, p_radius.y));
		const float step = (p_angleMax - p_angleMin) / static_cast<float>(segments);
		const float cosRot = std::cos(p_rotation);
		const float sinRot = std::sin(p_rotation);
		for (int i = 0; i <= segments; ++i)
		{
			const float angle = p_angleMin + step * static_cast<float>(i);
			const float x = std::cos(angle) * p_radius.x;
			const float y = std::sin(angle) * p_radius.y;
			m_path.push_back(Vec2(
				p_center.x + x * cosRot - y * sinRot,
				p_center.y + x * sinRot + y * cosRot));
		}
	}

	void DrawList::PathBezierCubicCurveTo(const Vec2& p_p2, const Vec2& p_p3, const Vec2& p_p4, int p_segments)
	{
		if (m_path.empty())
		{
			return;
		}
		const Vec2 p1 = m_path.back();
		// A rough control-polygon length stands in for the arc length; one segment per few
		// pixels is plenty at overlay scales.
		const float hullLength = Length(p_p2 - p1) + Length(p_p3 - p_p2) + Length(p_p4 - p_p3);
		const int segments = p_segments > 0 ? p_segments
			: std::clamp(static_cast<int>(hullLength / 6.0f), 4, 64);
		for (int i = 1; i <= segments; ++i)
		{
			const float t = static_cast<float>(i) / static_cast<float>(segments);
			const float u = 1.0f - t;
			const float w0 = u * u * u;
			const float w1 = 3.0f * u * u * t;
			const float w2 = 3.0f * u * t * t;
			const float w3 = t * t * t;
			m_path.push_back(Vec2(
				w0 * p1.x + w1 * p_p2.x + w2 * p_p3.x + w3 * p_p4.x,
				w0 * p1.y + w1 * p_p2.y + w2 * p_p3.y + w3 * p_p4.y));
		}
	}

	void DrawList::PathBezierQuadraticCurveTo(const Vec2& p_p2, const Vec2& p_p3, int p_segments)
	{
		if (m_path.empty())
		{
			return;
		}
		const Vec2 p1 = m_path.back();
		const float hullLength = Length(p_p2 - p1) + Length(p_p3 - p_p2);
		const int segments = p_segments > 0 ? p_segments
			: std::clamp(static_cast<int>(hullLength / 6.0f), 4, 64);
		for (int i = 1; i <= segments; ++i)
		{
			const float t = static_cast<float>(i) / static_cast<float>(segments);
			const float u = 1.0f - t;
			const float w0 = u * u;
			const float w1 = 2.0f * u * t;
			const float w2 = t * t;
			m_path.push_back(Vec2(
				w0 * p1.x + w1 * p_p2.x + w2 * p_p3.x,
				w0 * p1.y + w1 * p_p2.y + w2 * p_p3.y));
		}
	}

	void DrawList::PathRect(const Vec2& p_min, const Vec2& p_max, float p_rounding, DrawFlags p_flags)
	{
		p_rounding = std::min(p_rounding, std::min(p_max.x - p_min.x, p_max.y - p_min.y) * 0.5f);
		if (p_rounding <= 0.5f || HasFlag(p_flags, static_cast<DrawFlags>(1u << 8)))
		{
			PathLineTo(Vec2(p_min.x, p_min.y));
			PathLineTo(Vec2(p_max.x, p_min.y));
			PathLineTo(Vec2(p_max.x, p_max.y));
			PathLineTo(Vec2(p_min.x, p_max.y));
			return;
		}

		const bool all = HasFlag(p_flags, DrawFlags::RoundCornersAll);
		const bool topLeft = all || HasFlag(p_flags, DrawFlags::RoundCornersTopLeft);
		const bool topRight = all || HasFlag(p_flags, DrawFlags::RoundCornersTopRight);
		const bool bottomLeft = all || HasFlag(p_flags, DrawFlags::RoundCornersBottomLeft);
		const bool bottomRight = all || HasFlag(p_flags, DrawFlags::RoundCornersBottomRight);

		if (topLeft)
		{
			PathArcTo(Vec2(p_min.x + p_rounding, p_min.y + p_rounding), p_rounding, kPi, kPi * 1.5f, 4);
		}
		else
		{
			PathLineTo(Vec2(p_min.x, p_min.y));
		}
		if (topRight)
		{
			PathArcTo(Vec2(p_max.x - p_rounding, p_min.y + p_rounding), p_rounding, kPi * 1.5f, kPi * 2.0f, 4);
		}
		else
		{
			PathLineTo(Vec2(p_max.x, p_min.y));
		}
		if (bottomRight)
		{
			PathArcTo(Vec2(p_max.x - p_rounding, p_max.y - p_rounding), p_rounding, 0.0f, kPi * 0.5f, 4);
		}
		else
		{
			PathLineTo(Vec2(p_max.x, p_max.y));
		}
		if (bottomLeft)
		{
			PathArcTo(Vec2(p_min.x + p_rounding, p_max.y - p_rounding), p_rounding, kPi * 0.5f, kPi, 4);
		}
		else
		{
			PathLineTo(Vec2(p_min.x, p_max.y));
		}
	}

	void DrawList::PathFillConvex(ImU32 p_col)
	{
		if (m_path.size() >= 3)
		{
			AddConvexPolyFilled(m_path.data(), static_cast<int>(m_path.size()), p_col);
		}
		m_path.clear();
	}

	void DrawList::PathStroke(ImU32 p_col, float p_thickness, DrawFlags p_flags)
	{
		if (m_path.size() >= 2)
		{
			AddPolyline(m_path.data(), static_cast<int>(m_path.size()), p_col, p_thickness, p_flags);
		}
		m_path.clear();
	}

	// ---------------------------------------------------------------------------------------
	// Textures
	// ---------------------------------------------------------------------------------------
	void DrawList::AddImage(TextureId p_texture, const Vec2& p_min, const Vec2& p_max, ImU32 p_tint,
		const Vec2& p_uvMin, const Vec2& p_uvMax)
	{
		AddImageQuad(p_texture, p_min, Vec2(p_max.x, p_min.y), p_max, Vec2(p_min.x, p_max.y), p_tint,
			Vec2(p_uvMin.x, p_uvMin.y), Vec2(p_uvMax.x, p_uvMin.y), Vec2(p_uvMax.x, p_uvMax.y),
			Vec2(p_uvMin.x, p_uvMax.y));
	}

	void DrawList::PrimUnreserve(int p_indexCount, int p_vertexCount)
	{
		// Only the tail of the buffer can be given back: a reservation is always contiguous and
		// nothing is written past it.
		if (p_indexCount > 0 && static_cast<size_t>(p_indexCount) <= m_indices.size())
		{
			m_indices.resize(m_indices.size() - static_cast<size_t>(p_indexCount));
			m_primIdxCursor = static_cast<uint32_t>(m_indices.size());
		}
		if (p_vertexCount > 0 && static_cast<size_t>(p_vertexCount) <= m_vertices.size())
		{
			m_vertices.resize(m_vertices.size() - static_cast<size_t>(p_vertexCount));
			m_primVtxCursor = static_cast<uint32_t>(m_vertices.size());
		}
	}

	void DrawList::AddImageRounded(TextureId p_texture, const Vec2& p_min, const Vec2& p_max, ImU32 p_tint,
		float p_rounding, DrawFlags p_flags, const Vec2& p_uvMin, const Vec2& p_uvMax)
	{
		if (p_rounding <= 0.5f)
		{
			AddImage(p_texture, p_min, p_max, p_tint, p_uvMin, p_uvMax);
			return;
		}

		// The rounded outline is convex, so a fan over its vertices fills it. The UVs stay
		// affine over the rectangle - (x - min) / (max - min) - which is what the texture
		// coordinates of a plain AddImage are as well.
		PathClear();
		PathRect(p_min, p_max, p_rounding, p_flags);
		const int count = static_cast<int>(m_path.size());
		if (count < 3)
		{
			PathClear();
			return;
		}
		const float width = std::max(p_max.x - p_min.x, 1.0e-6f);
		const float height = std::max(p_max.y - p_min.y, 1.0e-6f);

		PushTexture(p_texture);
		PrimReserve((count - 2) * 3, count);
		for (int i = 0; i < count; ++i)
		{
			const Vec2& p = m_path[static_cast<size_t>(i)];
			const Vec2 uv(
				p_uvMin.x + (p.x - p_min.x) / width * (p_uvMax.x - p_uvMin.x),
				p_uvMin.y + (p.y - p_min.y) / height * (p_uvMax.y - p_uvMin.y));
			PrimWriteVertex(p, uv, p_tint);
		}
		for (int i = 2; i < count; ++i)
		{
			PrimWriteIndex(m_primVtxStart);
			PrimWriteIndex(m_primVtxStart + i - 1);
			PrimWriteIndex(m_primVtxStart + i);
		}
		PopTexture();
		PathClear();
	}
	void DrawList::AddImageQuad(TextureId p_texture, const Vec2& p_p1, const Vec2& p_p2, const Vec2& p_p3,
		const Vec2& p_p4, ImU32 p_tint, const Vec2& p_uv1, const Vec2& p_uv2, const Vec2& p_uv3, const Vec2& p_uv4)
	{
		PushTexture(p_texture);
		PrimQuadUV(p_p1, p_p2, p_p3, p_p4, p_uv1, p_uv2, p_uv3, p_uv4, p_tint);
		PopTexture();
	}

	// ---------------------------------------------------------------------------------------
	// Text
	// ---------------------------------------------------------------------------------------
	Vec2 DrawList::AddText(Font& p_font, float p_size, const Vec2& p_pos, ImU32 p_col, const char* p_textBegin,
		const char* p_textEnd)
	{
		return AddText(p_font, p_size, 1.0f, p_pos, p_col, p_textBegin, p_textEnd);
	}

	Vec2 DrawList::AddText(Font& p_font, float p_size, const Vec2& p_pos, ImU32 p_col, const char* p_textBegin,
		const char* p_textEnd, float p_wrapWidth)
	{
		return AddText(p_font, p_size, 1.0f, p_pos, p_col, p_textBegin, p_textEnd, p_wrapWidth);
	}

	Vec2 DrawList::AddText(Font& p_font, float p_size, float p_uiScale, const Vec2& p_pos, ImU32 p_col,
		const char* p_textBegin, const char* p_textEnd, float p_wrapWidth)
	{
		if (!p_font.IsLoaded() || p_textBegin == nullptr)
		{
			return p_pos;
		}
		if (p_textEnd == nullptr)
		{
			p_textEnd = p_textBegin + std::strlen(p_textBegin);
		}

		// The font bakes one atlas per requested pixel size, so there is no scaling here: the
		// glyphs come out at the size they will be drawn at. p_uiScale is kept for callers that
		// want a proportional bump (a DPI factor), and only then does anything scale.
		const float requestedSize = std::max(1.0f, p_size > 0.0f ? p_size : 16.0f) * p_uiScale;
		const float scale = 1.0f;
		const float lineHeight = p_font.GetLineHeight(requestedSize);
		const float ascent = p_font.GetAscent(requestedSize);
		bool texturePushed = false;
		const auto ensureTexture = [&](int p_atlasIndex) {
			if (!texturePushed)
			{
				PushTexture(p_font.GetTextureId(p_atlasIndex));
				texturePushed = true;
			}
		};

		Vec2 cursor(p_pos.x, p_pos.y + ascent);
		const float lineStartX = p_pos.x;
		float maxX = p_pos.x;
		// The last position a line could break at: a space, or any position when a single word
		// is longer than the wrap width (breaking mid-word beats overflowing).
		float lastBreakX = lineStartX;
		const char* breakCursor = nullptr;

		const char* s = p_textBegin;
		while (s < p_textEnd)
		{
			const char* glyphStart = s;
			const uint32_t codepoint = Im2DDecodeUtf8(s, p_textEnd);
			if (codepoint == '\n')
			{
				maxX = std::max(maxX, cursor.x);
				cursor.x = lineStartX;
				cursor.y += lineHeight;
				lastBreakX = lineStartX;
				breakCursor = nullptr;
				continue;
			}
			if (codepoint == '\r')
			{
				continue;
			}

			const Glyph* glyph = p_font.FindGlyph(codepoint, requestedSize);
			if (glyph == nullptr && codepoint != '?')
			{
				glyph = p_font.FindGlyph('?', requestedSize);
			}
			if (glyph == nullptr)
			{
				continue;
			}
			ensureTexture(glyph->atlasIndex);


			// Wrap: once the next glyph would pass the wrap width, retry from the last space, or
			// from this glyph when there is none.
			if (p_wrapWidth > 0.0f
				&& cursor.x + glyph->advanceX * scale > lineStartX + p_wrapWidth
				&& cursor.x > lineStartX)
			{
				maxX = std::max(maxX, cursor.x);
				cursor.y += lineHeight;
				if (breakCursor != nullptr)
				{
					cursor.x = lastBreakX;
					s = breakCursor;
					while (s < p_textEnd && (*s == ' ' || *s == '\t'))
					{
						++s;
					}
				}
				else
				{
					cursor.x = lineStartX;
				}
				lastBreakX = lineStartX;
				breakCursor = nullptr;
				continue;
			}

			if (glyph->width > 0.0f && glyph->height > 0.0f)
			{
				const Vec2 a(cursor.x + glyph->offsetX * scale, cursor.y + glyph->offsetY * scale);
				const Vec2 b(a.x + glyph->width * scale, a.y);
				const Vec2 c(b.x, a.y + glyph->height * scale);
				const Vec2 d(a.x, c.y);

				PrimQuadUV(a, b, c, d,
					Vec2(glyph->u0, glyph->v0), Vec2(glyph->u1, glyph->v0),
					Vec2(glyph->u1, glyph->v1), Vec2(glyph->u0, glyph->v1), p_col);
			}
			cursor.x += glyph->advanceX * scale;
			if (codepoint == ' ' || codepoint == '\t')
			{
				lastBreakX = cursor.x;
				breakCursor = glyphStart;
			}
		}

		if (texturePushed)
		{
			PopTexture();
		}
		maxX = std::max(maxX, cursor.x);
		return Vec2(p_pos.x, cursor.y - ascent + lineHeight);
	}

	Vec2 DrawList::CalcTextSize(Font& p_font, float p_size, const char* p_textBegin, const char* p_textEnd)
	{
		if (!p_font.IsLoaded() || p_textBegin == nullptr)
		{
			return Vec2();
		}
		if (p_textEnd == nullptr)
		{
			p_textEnd = p_textBegin + std::strlen(p_textBegin);
		}
		const float requestedSize = std::max(1.0f, p_size > 0.0f ? p_size : 16.0f);

		float width = 0.0f;
		float maxWidth = 0.0f;
		int lines = 1;
		const char* s = p_textBegin;
		while (s < p_textEnd)
		{
			const uint32_t codepoint = Im2DDecodeUtf8(s, p_textEnd);
			if (codepoint == '\n')
			{
				maxWidth = std::max(maxWidth, width);
				width = 0.0f;
				++lines;
				continue;
			}
			const Glyph* glyph = p_font.FindGlyph(codepoint, requestedSize);
			if (glyph == nullptr)
			{
				glyph = p_font.FindGlyph('?', requestedSize);
			}
			if (glyph != nullptr)
			{
				width += glyph->advanceX;
			}
		}
		maxWidth = std::max(maxWidth, width);
		return Vec2(maxWidth, p_font.GetLineHeight(requestedSize) * static_cast<float>(lines));
	}
}

// ---------------------------------------------------------------------------------------
// Higher level shapes
// ---------------------------------------------------------------------------------------

namespace Rendering::Overlay::Im2D
{
	namespace
	{
		/** Rotates a local point by the transform's angle and translates it.
		 *
		 * The angle is measured counter-clockwise in the *mathematical* sense (x right, y up)
		 * and then mapped to a screen space whose y grows downwards - the same convention the
		 * dimension drawing used with ImGui, where a positive angle points the shaft up and to
		 * the right. Getting this backwards mirrors every arrow about the horizontal axis. */
		Vec2 TransformPoint(const Transform& p_transform, float p_x, float p_y)
		{
			const float rad = p_transform.angleDeg * 3.14159265358979f / 180.0f;
			const float c = std::cos(rad);
			const float s = std::sin(rad);
			return Vec2(p_transform.pos.x + p_x * c + p_y * s, p_transform.pos.y - p_x * s + p_y * c);
		}
	}

	void AddArrow(DrawList& p_list, const Transform& p_transform, ImU32 p_col, float p_length, float p_thickness)
	{
		const float head = 2.5f * p_thickness;
		p_list.AddLine(TransformPoint(p_transform, 0.0f, 0.0f), TransformPoint(p_transform, p_length, 0.0f),
			p_col, p_thickness);
		p_list.AddTriangleFilled(
			TransformPoint(p_transform, p_length, head),
			TransformPoint(p_transform, p_length, -head),
			TransformPoint(p_transform, p_length + 2.0f * head, 0.0f),
			p_col);
	}

	void AddDoubleArrow(DrawList& p_list, const Transform& p_transform, ImU32 p_col, float p_length, float p_thickness)
	{
		const float head = 4.0f * p_thickness;
		p_list.AddLine(TransformPoint(p_transform, 0.0f, 0.0f), TransformPoint(p_transform, p_length, 0.0f),
			p_col, p_thickness);
		// Heads point outward, tips exactly on the endpoints; the bases are inset by one head.
		p_list.AddTriangleFilled(
			TransformPoint(p_transform, p_length - 2.0f * head, head),
			TransformPoint(p_transform, p_length - 2.0f * head, -head),
			TransformPoint(p_transform, p_length, 0.0f),
			p_col);
		p_list.AddTriangleFilled(
			TransformPoint(p_transform, 0.0f, 0.0f),
			TransformPoint(p_transform, 2.0f * head, head),
			TransformPoint(p_transform, 2.0f * head, -head),
			p_col);
	}

	void AddDoubleArrow(DrawList& p_list, Font& p_font, float p_fontSize, const Transform& p_transform, ImU32 p_col,
		float p_length, float p_thickness, const char* p_text)
	{
		AddDoubleArrow(p_list, p_transform, p_col, p_length, p_thickness);
		if (p_text == nullptr || p_text[0] == '\0' || !p_font.IsLoaded())
		{
			return;
		}
		// Centred above the shaft, bottom edge a small gap away from the line. The label stays
		// horizontal: a rotated value is harder to read than a straight one, and this is what
		// the dimension previews showed before.
		const Vec2 size = DrawList::CalcTextSize(p_font, p_fontSize, p_text);
		const Vec2 mid = TransformPoint(p_transform, p_length * 0.5f, 0.0f);
		p_list.AddText(p_font, p_fontSize, Vec2(mid.x - size.x * 0.5f, mid.y - 3.0f - size.y), p_col, p_text);
	}

	void AddDoubleArcArrow(DrawList& p_list, Font& p_font, float p_fontSize, const Transform& p_transform,
		ImU32 p_col, float p_radius, float p_degree, float p_thickness, const char* p_text)
	{
		AddDoubleArcArrow(p_list, p_transform, p_col, p_radius, p_degree, p_thickness);
		if (p_text == nullptr || p_text[0] == '\0' || !p_font.IsLoaded())
		{
			return;
		}
		const float pi = 3.14159265358979f;
		const float start = p_transform.angleDeg * pi / 180.0f;
		const float sweep = p_degree * pi / 180.0f;
		const float midAngle = start + sweep * 0.5f;
		const Vec2 size = DrawList::CalcTextSize(p_font, p_fontSize, p_text);
		// Outside the arc's midpoint, horizontally centred on the outward direction.
		const float distance = p_radius + 3.0f + size.y * 0.5f;
		const Vec2 center(p_transform.pos.x + std::cos(midAngle) * distance,
			p_transform.pos.y + std::sin(midAngle) * distance);
		p_list.AddText(p_font, p_fontSize, Vec2(center.x - size.x * 0.5f, center.y + size.y * 0.5f), p_col, p_text);
	}
	void AddDoubleArcArrow(DrawList& p_list, const Transform& p_transform, ImU32 p_col, float p_radius,
		float p_degree, float p_thickness)
	{
		if (p_radius <= 0.01f || std::fabs(p_degree) < 1e-3f)
		{
			return;
		}
		const float pi = 3.14159265358979f;
		const float start = p_transform.angleDeg * pi / 180.0f;
		const float sweep = p_degree * pi / 180.0f;
		const float end = start + sweep;

		// The arc as a polyline (five degrees a segment, at least eight).
		const int segments = std::max(8, static_cast<int>(std::ceil(std::fabs(p_degree) / 5.0f)));
		const int pointCount = segments + 1;
		std::vector<Vec2> points(static_cast<size_t>(pointCount));
		for (int i = 0; i < pointCount; ++i)
		{
			const float t = static_cast<float>(i) / static_cast<float>(segments);
			const float a = start + (end - start) * t;
			points[static_cast<size_t>(i)] = Vec2(p_transform.pos.x + std::cos(a) * p_radius,
				p_transform.pos.y + std::sin(a) * p_radius);
		}
		p_list.AddPolyline(points.data(), pointCount, p_col, p_thickness);

		// Heads whose tips sit on the arc endpoints, pointing along the sweep.
		const float head = 4.0f * p_thickness;
		const float sweepSign = sweep >= 0.0f ? 1.0f : -1.0f;
		const auto drawHead = [&](const Vec2& p_tip, const Vec2& p_dirOut)
		{
			const Vec2 base(p_tip.x - p_dirOut.x * 2.0f * head, p_tip.y - p_dirOut.y * 2.0f * head);
			const Vec2 perp(-p_dirOut.y, p_dirOut.x);
			p_list.AddTriangleFilled(
				Vec2(base.x + perp.x * head, base.y + perp.y * head),
				Vec2(base.x - perp.x * head, base.y - perp.y * head),
				p_tip, p_col);
		};
		const Vec2 startTip(points.front());
		const Vec2 endTip(points.back());
		drawHead(startTip, Vec2(std::sin(start) * sweepSign, -std::cos(start) * sweepSign));
		drawHead(endTip, Vec2(-std::sin(end) * sweepSign, std::cos(end) * sweepSign));
	}

	void AddTextRotated(DrawList& p_list, Font& p_font, float p_size, const Transform& p_transform, ImU32 p_col,
		const char* p_text)
	{
		if (!p_font.IsLoaded() || p_text == nullptr)
		{
			return;
		}
		const float requestedSize = std::max(1.0f, p_size > 0.0f ? p_size : 16.0f);
		// Glyphs come from the atlas of exactly this size; nothing is scaled here.
		constexpr float scale = 1.0f;
		const float ascent = p_font.GetAscent(requestedSize);

		// The pen walks along the rotated baseline; every glyph corner is rotated with it.
		float penX = 0.0f;
		const char* cursor = p_text;
		const char* end = p_text + std::strlen(p_text);
		while (cursor < end)
		{
			const uint32_t codepoint = Im2DDecodeUtf8(cursor, end);
			if (codepoint == '\n' || codepoint == '\r')
			{
				continue;
			}
			const Glyph* glyph = p_font.FindGlyph(codepoint, requestedSize);
			if (glyph == nullptr)
			{
				glyph = p_font.FindGlyph('?', requestedSize);
			}
			if (glyph == nullptr)
			{
				continue;
			}
			if (glyph->width > 0.0f && glyph->height > 0.0f)
			{
				const float x0 = penX + glyph->offsetX * scale;
				const float y0 = -ascent + glyph->offsetY * scale;
				const float x1 = x0 + glyph->width * scale;
				const float y1 = y0 + glyph->height * scale;
				const auto corner = [&](float p_x, float p_y)
				{
					return TransformPoint(p_transform, p_x, p_y);
				};
				p_list.AddImageQuad(p_font.GetTextureId(glyph->atlasIndex),
					corner(x0, y0), corner(x1, y0), corner(x1, y1), corner(x0, y1), p_col,
					Vec2(glyph->u0, glyph->v0), Vec2(glyph->u1, glyph->v0),
					Vec2(glyph->u1, glyph->v1), Vec2(glyph->u0, glyph->v1));
			}
			penX += glyph->advanceX * scale;
		}
	}
}