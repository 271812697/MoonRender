

#include "Rendering/Utils/ShaderUtil.h"

namespace Rendering::Utils
{
	std::string GetShaderTypeName(Settings::EShaderType p_type)
	{
		switch (p_type)
		{
		case Rendering::Settings::EShaderType::VERTEX: return "Vertex";
		case Rendering::Settings::EShaderType::FRAGMENT: return "Fragment";
		case Rendering::Settings::EShaderType::GEOMETRY: return "Geometry";
		case Rendering::Settings::EShaderType::COMPUTE: return "Compute";
		case Rendering::Settings::EShaderType::TESSELLATION_CONTROL: return "Tessellation Control";
		case Rendering::Settings::EShaderType::TESSELLATION_EVALUATION: return "Tessellation Evaluation";
		}

		return "None";
	}
}
