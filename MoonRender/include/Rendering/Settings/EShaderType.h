#pragma once
#include <cstdint>

namespace Rendering::Settings
{
	/**
	* Shader stages the GL backend can compile and attach. The set matches the stages OpenGL
	* 4.5 knows about (the same list baregl/Overload settled on):
	*  - VERTEX, FRAGMENT and GEOMETRY are what the material packages compile today;
	*  - COMPUTE is a program on its own (dispatched, not drawn) - the type is here so the HAL
	*    can create and attach it, the material DSL and the draw path do not use it yet;
	*  - TESSELLATION_CONTROL/EVALUATION complete the pipeline for tessellated geometry.
	*/
	enum class EShaderType : uint8_t
	{
		NONE,
		VERTEX,
		GEOMETRY,
		/** The old spelling of GEOMETRY, kept so existing code still compiles. */
		GEOMERTY = GEOMETRY,
		FRAGMENT,
		COMPUTE,
		TESSELLATION_CONTROL,
		TESSELLATION_EVALUATION,
	};
}
