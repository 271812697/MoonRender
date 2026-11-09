#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

#include <Eigen/Core>

#include "Interactive/Im3DEffect.h"
#include <Interactive/MathUtil/MathUtil.h>

namespace Rendering::Data
{
	class Material;
}
namespace Rendering::Overlay
{
	class Im3DGLRenderer;
}

namespace MOON
{
	/** Shared helpers for the effects that draw lines.
	 *
	 * Line effects all produce the same base vertex stream (VertexFormat plus a few floats of
	 * effect data) and each keeps one vertex buffer per frame, so Draw() is a single draw call.
	 * The helpers only project a polyline, append its vertices and issue that draw; what the
	 * extra floats mean stays with the effect. */
	class ALineEffect : public AImEffect
	{
	public:
		ALineEffect(std::string p_id, const ImEffectDesc& p_desc);

	protected:
		/** Appends one polyline (two vertices per segment) to p_outVertices, measuring each point's
		 * distance from the start on screen - what keeps a pattern or a colour ramp the same size
		 * under zoom. p_fillExtra runs per point with its index, distance and extra-float slot. */
		template <class TFillExtra>
		void AppendPolyline(std::vector<uint8_t>& p_outVertices,
			const std::vector<Eigen::Vector3f>& p_points, float p_size,
			Eigen::Vector4<uint8_t> p_color, TFillExtra&& p_fillExtra)
		{
			if (p_points.size() < 2)
			{
				return;
			}
			const unsigned int extraFloats = ExtraFloats();
			const size_t stride = StrideBytes();
			p_outVertices.reserve(p_outVertices.size() + (p_points.size() - 1) * 2 * stride);

			const auto project = [this](const Eigen::Vector3f& p_point)
			{
				const Eigen::Vector3f ndc = MatrixMulPoint(Context().viewProj, p_point);
				return Eigen::Vector2f((ndc.x() + 1.0f) * 0.5f * Context().viewportWidth,
					(1.0f - ndc.y()) * 0.5f * Context().viewportHeight);
			};

			// Distances for the whole polyline first: a ramp needs the total, which is only known
			// at the end.
			std::vector<float> distances(p_points.size(), 0.0f);
			float total = 0.0f;
			Eigen::Vector2f previous = project(p_points.front());
			for (size_t i = 1; i < p_points.size(); ++i)
			{
				const Eigen::Vector2f current = project(p_points[i]);
				total += (current - previous).norm();
				distances[i] = total;
				previous = current;
			}

			float extras[8] = {};
			const auto append = [&](size_t p_index)
			{
				const VertexFormat vertex(p_points[p_index], p_size, p_color);
				const size_t offset = p_outVertices.size();
				p_outVertices.resize(offset + stride);
				std::memcpy(p_outVertices.data() + offset, &vertex, sizeof(VertexFormat));
				p_fillExtra(p_index, distances[p_index], total, extras);
				std::memcpy(p_outVertices.data() + offset + sizeof(VertexFormat), extras,
					extraFloats * sizeof(float));
			};
			for (size_t i = 0; i + 1 < p_points.size(); ++i)
			{
				append(i);
				append(i + 1);
			}
		}

		/** Draws the whole buffer in one call. */
		void DrawBuffer(::Rendering::Overlay::Im3DGLRenderer& p_gl,
			Rendering::Data::Material& p_material, const std::vector<uint8_t>& p_vertices);
	};
}
