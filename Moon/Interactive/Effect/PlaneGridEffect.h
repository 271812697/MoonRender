#pragma once
#include <vector>

#include <Rendering/Data/Material.h>

#include "Interactive/Im3DEffect.h"

namespace MOON
{
	/** The sketch's background grid, drawn as a quad of the sketch plane.
	 *
	 * The quad is geometry: the sketch hands over its vertices - the visible part of the plane,
	 * already split at the clip planes the way the line-built grid split its segments - and the
	 * vertex stage projects them. So the depth comes from the vertex pipeline, like every other
	 * overlay primitive, and the grid is occluded by the model exactly where the model stands in
	 * front of the plane.
	 *
	 * The pattern is evaluated in the fragment stage from the plane coordinates of each vertex:
	 * one-pixel lines at any zoom, two levels that cross-fade with their on-screen spacing, no
	 * moire on a grazing view.
	 *
	 * The grid is a per-frame effect: the sketch appends the quad for the frame being built
	 * (OnBeginFrame drops the previous one) and Draw() consumes it. */
	class PlaneGridEffect : public AImEffect
	{
	public:
		PlaneGridEffect();

		/** Arms the grid for this frame with its cell size and colours. */
		void SetStyle(float p_step,
			const Maths::FVector4& p_minorColor, const Maths::FVector4& p_majorColor,
			float p_lineWidthPx);

		/** Appends one vertex of the quad, in triangle order: a world position and the sketch
		 * plane (u, v) it stands for. */
		void AddVertex(const Eigen::Vector3f& p_position, float p_u, float p_v);

		void Draw(::Rendering::Overlay::Im3DGLRenderer& p_gl) override;

	protected:
		void OnBeginFrame() override;

	private:
		Rendering::Data::Material m_material;
		std::vector<uint8_t> m_vertices;
		unsigned int m_vertexCount = 0;

		Maths::FVector4 m_minorColor{ 0.55f, 0.55f, 0.55f, 0.18f };
		Maths::FVector4 m_majorColor{ 0.45f, 0.45f, 0.45f, 0.35f };
		float m_step = 10.0f;
		float m_lineWidthPx = 1.2f;
	};
}
