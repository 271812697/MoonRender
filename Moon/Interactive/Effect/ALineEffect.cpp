#include "Interactive/Effect/ALineEffect.h"

#include <glad/glad.h>

#include <Interactive/MathUtil/MathUtil.h>
#include <Rendering/Overlay/Im3DGLRenderer.h>

namespace MOON
{
	ALineEffect::ALineEffect(std::string p_id, const ImEffectDesc& p_desc)
		: AImEffect(std::move(p_id), p_desc)
	{
	}

	void ALineEffect::DrawBuffer(::Rendering::Overlay::Im3DGLRenderer& p_gl,
		Rendering::Data::Material& p_material, const std::vector<uint8_t>& p_vertices)
	{
		const unsigned int stride = StrideBytes();
		const unsigned int count = static_cast<unsigned int>(p_vertices.size() / stride);
		if (count == 0)
		{
			return;
		}
		p_gl.Draw(p_material, Desc().topology, Desc().topology == GL_LINES,
			p_vertices.data(), count, stride,
			Context().viewProjT.data(), Context().viewportWidth, Context().viewportHeight,
			ExtraFloats());
		AddDrawCall();
	}
}
