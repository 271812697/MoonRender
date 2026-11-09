#include "Interactive/Im3DEffect.h"

#include <Tools/Utils/PathParser.h>

namespace MOON
{
	AImEffect::AImEffect(std::string p_id, const ImEffectDesc& p_desc)
		: m_id(std::move(p_id)), m_desc(p_desc)
	{
	}

	AImEffect::~AImEffect() = default;

	std::string ImShaderPath(const char* p_fileName)
	{
		return Tools::Utils::PathParser::GetExeDirectory() + "/Moon/Data/Engine/Shaders/" + p_fileName;
	}
}
