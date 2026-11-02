#pragma once
#include <Eigen/Core>

#include <cmath>
#include <vector>

/** Triangle-list builders shared by the immediate-mode gizmo widgets (Gizmo2DWidget,
 * Gizmo3DWidget). Everything here works in the widget's own local space: the widget bakes the
 * pivot and basis into each face's model matrix, so these only ever see unit axes.
 *
 * The drawing path is the immediate-mode one (ImRenderer::drawTriangleList), not the gizmo
 * shader: these helpers just append triangles to a WidgetViewData face. */
namespace MOON::GizmoGeometry
{
	inline constexpr float kPi = 3.14159265358979323846f;

	/** The immediate-mode triangle pass uploads its colour as aColor.abgr and blends with the
	 * alpha in it (see GizmoTriangle.shader), so the four bytes are packed a, b, g, r. */
	inline Eigen::Vector4<uint8_t> Rgba(uint8_t p_r, uint8_t p_g, uint8_t p_b, uint8_t p_a = 255)
	{
		return { p_a, p_b, p_g, p_r };
	}

	inline void AddTriangle(std::vector<Eigen::Vector3f>& p_out, const Eigen::Vector3f& p_a,
		const Eigen::Vector3f& p_b, const Eigen::Vector3f& p_c)
	{
		p_out.push_back(p_a);
		p_out.push_back(p_b);
		p_out.push_back(p_c);
	}

	/** p_a -> p_b -> p_c -> p_d, in order around the quad. */
	inline void AddQuad(std::vector<Eigen::Vector3f>& p_out, const Eigen::Vector3f& p_a,
		const Eigen::Vector3f& p_b, const Eigen::Vector3f& p_c, const Eigen::Vector3f& p_d)
	{
		AddTriangle(p_out, p_a, p_b, p_c);
		AddTriangle(p_out, p_a, p_c, p_d);
	}

	/** Two unit vectors perpendicular to p_axis. */
	inline void PerpendicularBasis(const Eigen::Vector3f& p_axis, Eigen::Vector3f& p_u,
		Eigen::Vector3f& p_v)
	{
		const Eigen::Vector3f up =
			std::abs(p_axis.y()) < 0.9f ? Eigen::Vector3f(0, 1, 0) : Eigen::Vector3f(1, 0, 0);
		p_u = p_axis.cross(up).normalized();
		p_v = p_axis.cross(p_u).normalized();
	}

	/** A tapered tube from p_from to p_to (a cylinder when both radii match, a cone tip when
	 * p_radiusTo is 0). The arrow shafts and heads are built from it. */
	inline void AddTube(std::vector<Eigen::Vector3f>& p_out, const Eigen::Vector3f& p_from,
		const Eigen::Vector3f& p_to, float p_radiusFrom, float p_radiusTo, int p_segments)
	{
		Eigen::Vector3f u;
		Eigen::Vector3f v;
		PerpendicularBasis((p_to - p_from).normalized(), u, v);
		for (int i = 0; i < p_segments; ++i)
		{
			const float a0 = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(p_segments);
			const float a1 = 2.0f * kPi * static_cast<float>(i + 1) / static_cast<float>(p_segments);
			const Eigen::Vector3f d0 = u * std::cos(a0) + v * std::sin(a0);
			const Eigen::Vector3f d1 = u * std::cos(a1) + v * std::sin(a1);
			AddQuad(p_out,
				p_from + d0 * p_radiusFrom, p_from + d1 * p_radiusFrom,
				p_to + d1 * p_radiusTo, p_to + d0 * p_radiusTo);
		}
	}

	/** A flat annulus in the plane whose normal is p_axis, centred on the origin. */
	inline void AddRing(std::vector<Eigen::Vector3f>& p_out, const Eigen::Vector3f& p_axis,
		float p_radius, float p_halfThickness, int p_segments)
	{
		Eigen::Vector3f u;
		Eigen::Vector3f v;
		PerpendicularBasis(p_axis, u, v);
		const float inner = p_radius - p_halfThickness;
		const float outer = p_radius + p_halfThickness;
		for (int i = 0; i < p_segments; ++i)
		{
			const float a0 = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(p_segments);
			const float a1 = 2.0f * kPi * static_cast<float>(i + 1) / static_cast<float>(p_segments);
			const Eigen::Vector3f d0 = u * std::cos(a0) + v * std::sin(a0);
			const Eigen::Vector3f d1 = u * std::cos(a1) + v * std::sin(a1);
			AddQuad(p_out, d0 * inner, d0 * outer, d1 * outer, d1 * inner);
		}
	}
}
