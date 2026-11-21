#pragma once
#include <string>

#include <Rendering/HAL/Common/TShaderStage.h>

namespace Rendering::HAL
{
	struct GLShaderStageContext
	{
		/** The GL shader name. 0 when the build runs on another thread: then this stage is only a
		 * name holder and the program's create command builds the real shader there. */
		uint32_t id = 0;
		Settings::EShaderType type = Settings::EShaderType::VERTEX;
		/** What Upload() was given. The program's create command carries it to the executor.
		 * Mutable because Upload() is const (the stage is passed around by const reference). */
		mutable std::string source;
	};

	using GLShaderStage = TShaderStage<Settings::EGraphicsBackend::OPENGL, GLShaderStageContext>;
}
