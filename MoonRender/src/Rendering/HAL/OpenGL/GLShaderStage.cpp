#include <algorithm>
#include <glad/glad.h>

#include <Rendering/HAL/OpenGL/GLShaderStage.h>
#include <Rendering/HAL/OpenGL/GLTypes.h>
#include <Rendering/HAL/Common/HALDriverAccess.h>
#include <Rendering/Context/Driver.h>
#include <Rendering/Utils/ShaderUtil.h>

namespace
{
	/** Whether a driver exists to record into: then the GL half of a stage is built by the
	 * create-program command, which compiles it on the side that owns the context - Filament's
	 * createShader. Recording is the same in both configurations: without a render thread the
	 * batch runs on this thread at the end of the frame (docs/RenderThreadPlan.md §8.72). */
	bool IsStageRecorded()
	{
		return ::Rendering::HAL::CurrentDriver() != nullptr;
	}
}

template<>
Rendering::HAL::GLShaderStage::TShaderStage(Settings::EShaderType p_type) : m_context{
	.type = p_type,
}
{
	if (!IsStageRecorded())
	{
		m_context.id = static_cast<uint32_t>(glCreateShader(EnumToValue<GLenum>(p_type)));
	}
}

template<>
Rendering::HAL::GLShaderStage::~TShaderStage()
{
	if (m_context.id != 0)
	{
		glDeleteShader(m_context.id);
	}
}

template<>
void Rendering::HAL::GLShaderStage::Upload(const std::string& p_source) const
{
	// The source is kept either way: it is what the program's create command compiles.
	m_context.source = p_source;
	if (m_context.id == 0)
	{
		return;
	}

	const char* source = p_source.c_str();
	glShaderSource(m_context.id, 1, &source, nullptr);
}

template<>
Rendering::Settings::ShaderCompilationResult Rendering::HAL::GLShaderStage::Compile() const
{
	if (m_context.id == 0)
	{
		// Compiled by the executor, as part of the program's create command. A failure is reported
		// there, with the driver's log (OpenGLDriver::CreateProgram).
		return {
			.success = true
		};
	}

	glCompileShader(m_context.id);

	GLint compileStatus;
	glGetShaderiv(m_context.id, GL_COMPILE_STATUS, &compileStatus);

	if (compileStatus == GL_FALSE)
	{
		GLint maxLength;
		glGetShaderiv(m_context.id, GL_INFO_LOG_LENGTH, &maxLength);

		std::string errorLog(maxLength, ' ');
		glGetShaderInfoLog(m_context.id, maxLength, &maxLength, errorLog.data());

		return {
			.success = false,
			.message = errorLog
		};
	}

	return {
		.success = true
	};
}

template<>
uint32_t Rendering::HAL::GLShaderStage::GetID() const
{
	return m_context.id;
}

template<>
Rendering::Settings::EShaderType Rendering::HAL::GLShaderStage::GetShaderType() const
{
	return m_context.type;
}

template<>
const std::string& Rendering::HAL::GLShaderStage::GetSource() const
{
	return m_context.source;
}
