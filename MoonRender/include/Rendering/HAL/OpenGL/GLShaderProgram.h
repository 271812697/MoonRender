#pragma once
#include <cstdint>
#include <Rendering/HAL/Common/TShaderProgram.h>

namespace Rendering::HAL
{
	/** The GL state of the backend object: the program's name. The compiled stages and the
	 * reflection live in the program object itself. */
	struct GLShaderProgramContext
	{
		uint32_t glId = 0;
	};

	using GLShaderProgram = TShaderProgram<Settings::EGraphicsBackend::OPENGL, GLShaderProgramContext>;
}
