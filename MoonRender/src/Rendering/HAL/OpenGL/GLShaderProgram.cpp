#include <glad/glad.h>

#include <array>
#include <cstdio>
#include <string>

#include <Rendering/HAL/OpenGL/GLProgramBindings.h>
#include <Rendering/HAL/OpenGL/GLShaderProgram.h>
#include <Rendering/HAL/OpenGL/GLTypes.h>

// The backend object of a program: everything here runs where the context is, on the object that
// lives in the handle's slot. The recording side never calls any of it - what a front end
// (Rendering::Data::Program) does is collect the stages and record the build.

namespace
{
	/** Filament keeps a program's uniform reflection on the backend program object: the queries need
	 * the program and the context, so they run here after the link. The recording side reads the
	 * result out of the object once the create command has run. */
	void ReflectUniforms(Rendering::HAL::GLShaderProgram& p_program)
	{
		p_program.uniforms.clear();
		p_program.uniformLocations.clear();

		GLint activeUniformCount = 0;
		glGetProgramiv(p_program.GetID(), GL_ACTIVE_UNIFORMS, &activeUniformCount);

		std::array<GLchar, 256> nameBuffer{};
		for (GLint i = 0; i < activeUniformCount; ++i)
		{
			GLint arraySize = 0;
			GLenum type = 0;
			GLsizei actualLength = 0;
			glGetActiveUniform(p_program.GetID(), static_cast<GLuint>(i),
				static_cast<GLsizei>(nameBuffer.size()), &actualLength, &arraySize, &type,
				nameBuffer.data());

			const std::string name{ nameBuffer.data(), static_cast<size_t>(actualLength) };
			// The engine's fixed blocks and the material block are written by offset; they have no
			// individual location, and asking for one is an invalid operation.
			if (name.starts_with("ubo_") || name.starts_with("ReflectionUBO"))
			{
				continue;
			}

			GLint blockIndex = -1;
			const GLuint uniformIndex = static_cast<GLuint>(i);
			glGetActiveUniformsiv(p_program.GetID(), 1, &uniformIndex, GL_UNIFORM_BLOCK_INDEX,
				&blockIndex);
			if (blockIndex != -1)
			{
				continue;
			}

			const GLint location = glGetUniformLocation(p_program.GetID(), name.c_str());
			p_program.uniformLocations.emplace(name, static_cast<uint32_t>(location));

			Rendering::Settings::UniformInfo info{
				.type = Rendering::HAL::ValueToEnum<Rendering::Settings::EUniformType>(type),
				.name = name,
				.defaultValue = std::any{}
			};
			p_program.uniforms.emplace(name, std::move(info));
		}
	}
}

template<>
Rendering::HAL::GLShaderProgram::TShaderProgram(BackendConstructionTag)
{
	// The backend half of the create: the slot exists (the recording side reserved it) and the
	// context is current, so the program object is made here (Filament's createProgramR).
	m_context.glId = glCreateProgram();
}

template<>
Rendering::Settings::ShaderLinkingResult Rendering::HAL::GLShaderProgram::Build(
	const std::vector<Rendering::Backend::ShaderSourceSpec>& p_stages)
{
	if (m_context.glId == 0)
	{
		return { .success = false, .message = "the GL program object could not be created" };
	}

	std::vector<GLuint> shaders;
	shaders.reserve(p_stages.size());
	for (const Rendering::Backend::ShaderSourceSpec& spec : p_stages)
	{
		if (spec.source == nullptr)
		{
			continue;
		}
		const GLuint shader = glCreateShader(EnumToValue<GLenum>(spec.type));
		if (shader == 0)
		{
			continue;
		}
		const char* source = spec.source->c_str();
		glShaderSource(shader, 1, &source, nullptr);
		glCompileShader(shader);

		GLint compileStatus = GL_FALSE;
		glGetShaderiv(shader, GL_COMPILE_STATUS, &compileStatus);
		if (compileStatus == GL_FALSE)
		{
			GLint length = 0;
			glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
			std::string log(static_cast<size_t>(length > 0 ? length : 1), '\0');
			glGetShaderInfoLog(shader, length, &length, log.data());
			std::fprintf(stderr, "[program] a stage failed to compile:\n%s\n", log.c_str());
			std::fflush(stderr);
		}
		shaders.push_back(shader);
	}

	for (const GLuint shader : shaders)
	{
		glAttachShader(m_context.glId, shader);
	}
	glLinkProgram(m_context.glId);

	GLint linkStatus = GL_FALSE;
	glGetProgramiv(m_context.glId, GL_LINK_STATUS, &linkStatus);

	std::string linkLog;
	if (linkStatus == GL_FALSE)
	{
		GLint length = 0;
		glGetProgramiv(m_context.glId, GL_INFO_LOG_LENGTH, &length);
		linkLog.assign(static_cast<size_t>(length > 0 ? length : 1), '\0');
		glGetProgramInfoLog(m_context.glId, length, &length, linkLog.data());
		std::fprintf(stderr, "[program] link failed:\n%s\n", linkLog.c_str());
		std::fflush(stderr);
	}

	// The stages are part of the program now: detaching and deleting them keeps only the compiled
	// program (which is what everything binds).
	for (const GLuint shader : shaders)
	{
		glDetachShader(m_context.glId, shader);
		glDeleteShader(shader);
	}

	if (linkStatus == GL_FALSE)
	{
		return { .success = false, .message = linkLog };
	}

	ReflectUniforms(*this);
	return { .success = true };
}

template<>
void Rendering::HAL::GLShaderProgram::Bind() const
{
	glUseProgram(m_context.glId);
}

template<>
void Rendering::HAL::GLShaderProgram::Unbind() const
{
	glUseProgram(0);
}

template<>
void Rendering::HAL::GLShaderProgram::DeclareBindings(
	const Rendering::Data::DescriptorSetInfo& p_bindings) const
{
	if (m_context.glId == 0)
	{
		return;
	}
	Rendering::HAL::DeclareProgramBindings(m_context.glId, p_bindings);
}

template<>
uint32_t Rendering::HAL::GLShaderProgram::GetID() const
{
	return m_context.glId;
}
