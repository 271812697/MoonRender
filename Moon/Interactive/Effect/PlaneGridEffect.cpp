#include "Interactive/Effect/PlaneGridEffect.h"

#include <glad/glad.h>

#include <cstring>

#include <Rendering/Overlay/Im3DGLRenderer.h>
#include <Rendering/Resources/Loaders/ShaderLoader.h>

namespace MOON
{
	PlaneGridEffect::PlaneGridEffect()
		: AImEffect("PlaneGrid", ImEffectDesc{
			.topology = GL_TRIANGLES,
			.channel = EImChannel::Backdrop,
			.extraFloats = 0 })
	{
		m_material.SetMaterial(Rendering::Resources::Loaders::ShaderLoader::Create(
			ImShaderPath("PlaneGrid.shader")));
	}

	void PlaneGridEffect::SetStyle(float p_step,
		const Maths::FVector4& p_minorColor, const Maths::FVector4& p_majorColor,
		float p_lineWidthPx)
	{
		m_step = p_step > 0.0f ? p_step : 1.0f;
		m_minorColor = p_minorColor;
		m_majorColor = p_majorColor;
		m_lineWidthPx = p_lineWidthPx > 0.0f ? p_lineWidthPx : 1.0f;
	}

	void PlaneGridEffect::AddVertex(const Eigen::Vector3f& p_position, float p_u, float p_v)
	{
		VertexFormat vertex(p_position, 1.0f, Eigen::Vector4<uint8_t>{ 255, 255, 255, 255 });
		vertex.uv = Eigen::Vector2f(p_u, p_v);
		const size_t offset = m_vertices.size();
		m_vertices.resize(offset + sizeof(VertexFormat));
		std::memcpy(m_vertices.data() + offset, &vertex, sizeof(VertexFormat));
		++m_vertexCount;
	}

	void PlaneGridEffect::OnBeginFrame()
	{
		// The quad is rebuilt every frame: it follows the camera, not the model.
		m_vertices.clear();
		m_vertexCount = 0;
	}

	void PlaneGridEffect::Draw(::Rendering::Overlay::Im3DGLRenderer& p_gl)
	{
		if (m_vertexCount < 3)
		{
			return;
		}

		const ImEffectContext& context = Context();
		m_material.SetProperty("uMinorColor", m_minorColor);
		m_material.SetProperty("uMajorColor", m_majorColor);
		m_material.SetProperty("uStep", m_step);
		m_material.SetProperty("uLineWidthPx", m_lineWidthPx);

		p_gl.Draw(m_material, Desc().topology, false,
			m_vertices.data(), m_vertexCount, StrideBytes(),
			context.viewProjT.data(), context.viewportWidth, context.viewportHeight,
			ExtraFloats());
		AddDrawCall();
	}
}
