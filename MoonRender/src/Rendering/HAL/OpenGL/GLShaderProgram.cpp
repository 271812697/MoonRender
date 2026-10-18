#include <array>
#include <glad/glad.h>

#include <Rendering/HAL/OpenGL/GLShaderProgram.h>
#include <Rendering/HAL/OpenGL/GLTexture.h>
#include <Rendering/HAL/OpenGL/GLTypes.h>
#include <Rendering/Resources/Texture.h>

namespace
{
	constexpr bool IsReservedUniform(std::string_view p_name)
	{
		return p_name.starts_with("ubo_") || p_name.starts_with("ReflectionUBO");
	}
}

template<>
Rendering::HAL::GLShaderProgram::TShaderProgram() : m_context{ .id = glCreateProgram() }
{

}

template<>
Rendering::HAL::GLShaderProgram::~TShaderProgram()
{
	glDeleteProgram(m_context.id);
}

template<>
void Rendering::HAL::GLShaderProgram::Bind() const
{
	glUseProgram(m_context.id);
}

template<>
void Rendering::HAL::GLShaderProgram::Unbind() const
{
	glUseProgram(0);
}

template<>
uint32_t Rendering::HAL::GLShaderProgram::GetID() const
{
	return m_context.id;
}

template<>
void Rendering::HAL::GLShaderProgram::Attach(const GLShaderStage& p_shader)
{
	glAttachShader(m_context.id, p_shader.GetID());
	m_context.attachedShaders.push_back(std::ref(p_shader));
}

template<>
void Rendering::HAL::GLShaderProgram::Detach(const GLShaderStage& p_shader)
{
	glDetachShader(m_context.id, p_shader.GetID());
	m_context.attachedShaders.erase(std::remove_if(
		m_context.attachedShaders.begin(),
		m_context.attachedShaders.end(),
		[&p_shader](const std::reference_wrapper<const GLShaderStage> shader) {
			return shader.get().GetID() == p_shader.GetID();
		}
	));
}

template<>
void Rendering::HAL::GLShaderProgram::DetachAll()
{
	for (auto& shader : m_context.attachedShaders)
	{
		glDetachShader(m_context.id, shader.get().GetID());
	}

	m_context.attachedShaders.clear();
}

template<>
Rendering::Settings::ShaderLinkingResult Rendering::HAL::GLShaderProgram::Link()
{
	glLinkProgram(m_context.id);

	GLint linkStatus;
	glGetProgramiv(m_context.id, GL_LINK_STATUS, &linkStatus);

	if (linkStatus == GL_FALSE)
	{
		GLint maxLength;
		glGetProgramiv(m_context.id, GL_INFO_LOG_LENGTH, &maxLength);

		std::string errorLog(maxLength, ' ');
		glGetProgramInfoLog(m_context.id, maxLength, &maxLength, errorLog.data());

		return {
			.success = false,
			.message = errorLog
		};
	}

	QueryUniforms();

	return {
		.success = true
	};
}

#define DECLARE_GET_UNIFORM_FUNCTION(type, glType, func) \
template<> \
template<> \
type Rendering::HAL::GLShaderProgram::GetUniform<type>(const std::string& p_name) \
{ \
	type result{}; \
	if (m_context.uniformsLocationCache.contains(p_name)) \
	{ \
		if (const uint32_t location = m_context.uniformsLocationCache.at(p_name)) \
		{ \
			func(m_context.id, location, reinterpret_cast<glType*>(&result)); \
		} \
	} \
	return result; \
}

DECLARE_GET_UNIFORM_FUNCTION(int, GLint, glGetUniformiv);
DECLARE_GET_UNIFORM_FUNCTION(float, GLfloat, glGetUniformfv);
DECLARE_GET_UNIFORM_FUNCTION(Maths::FVector2, GLfloat, glGetUniformfv);
DECLARE_GET_UNIFORM_FUNCTION(Maths::FVector3, GLfloat, glGetUniformfv);
DECLARE_GET_UNIFORM_FUNCTION(Maths::FVector4, GLfloat, glGetUniformfv);
DECLARE_GET_UNIFORM_FUNCTION(Maths::FMatrix3, GLfloat, glGetUniformfv);
DECLARE_GET_UNIFORM_FUNCTION(Maths::FMatrix4, GLfloat, glGetUniformfv);

#define DECLARE_SET_UNIFORM_FUNCTION(type, func, ...) \
template<> \
template<> \
void Rendering::HAL::GLShaderProgram::SetUniform<type>(const std::string& p_name, const type& value) \
{ \
	if (m_context.uniformsLocationCache.contains(p_name)) \
	{ \
		func(m_context.uniformsLocationCache.at(p_name), __VA_ARGS__); \
	} \
    else{\
        func(glGetUniformLocation(m_context.id, p_name.c_str()), __VA_ARGS__);\
	}\
}

DECLARE_SET_UNIFORM_FUNCTION(int, glUniform1i, value);
DECLARE_SET_UNIFORM_FUNCTION(float, glUniform1f, value);
DECLARE_SET_UNIFORM_FUNCTION(Maths::FVector2, glUniform2f, value.x, value.y);
DECLARE_SET_UNIFORM_FUNCTION(Maths::FVector3, glUniform3f, value.x, value.y, value.z);
DECLARE_SET_UNIFORM_FUNCTION(Maths::FVector4, glUniform4f, value.x, value.y, value.z, value.w);
DECLARE_SET_UNIFORM_FUNCTION(Maths::FMatrix3, glUniformMatrix3fv, 1, GL_TRUE, &value.data[0]);
DECLARE_SET_UNIFORM_FUNCTION(Maths::FMatrix4, glUniformMatrix4fv, 1, GL_TRUE, &value.data[0]);

template<>
void Rendering::HAL::GLShaderProgram::QueryUniforms()
{
	m_context.uniforms.clear();
	// A reloaded program has different locations: keeping the old ones would make SetUniform
	// write through locations that no longer belong to this program.
	m_context.uniformsLocationCache.clear();

	std::array<GLchar, 256> nameBuffer;

	GLint activeUniformCount = 0;
	glGetProgramiv(m_context.id, GL_ACTIVE_UNIFORMS, &activeUniformCount);

	for (GLint i = 0; i < activeUniformCount; ++i)
	{
		GLint arraySize = 0;
		GLenum type = 0;
		GLsizei actualLength = 0;

		glGetActiveUniform(m_context.id, i, static_cast<GLsizei>(nameBuffer.size()), &actualLength, &arraySize, &type, nameBuffer.data());

		const auto name = std::string{ nameBuffer.data(), static_cast<size_t>(actualLength) };
		const auto uniformType = ValueToEnum<Settings::EUniformType>(type);

		// Skip reserved uniforms (e.g. ubo uniforms)
		if (IsReservedUniform(name))
		{
			continue;
		}

		// Skip members of a uniform block (the per-material parameter block): they have no
		// individual location (glGetUniformLocation returns -1), so asking for one - or trying
		// to set one - is an invalid operation. The runtime writes them by std140 offset.
		GLint blockIndex = -1;
		{
			const GLuint uniformIndex = static_cast<GLuint>(i);
			glGetActiveUniformsiv(m_context.id, 1, &uniformIndex, GL_UNIFORM_BLOCK_INDEX, &blockIndex);
		}
		if (blockIndex != -1)
		{
			continue;
		}

		const auto location = glGetUniformLocation(m_context.id, name.c_str());
		
		m_context.uniformsLocationCache.emplace(name, static_cast<uint32_t>(location));

		const std::any uniformValue = [&]() -> std::any {
			switch (uniformType)
			{
				using enum Settings::EUniformType;
			case BOOL: return static_cast<bool>(GetUniform<int>(name));
			case INT: return GetUniform<int>(name);
			case FLOAT: return GetUniform<float>(name);
			case FLOAT_VEC2: return GetUniform<Maths::FVector2>(name);
			case FLOAT_VEC3: return GetUniform<Maths::FVector3>(name);
			case FLOAT_VEC4: return GetUniform<Maths::FVector4>(name);
			case FLOAT_MAT3: return GetUniform<Maths::FMatrix3>(name);
			case FLOAT_MAT4: return GetUniform<Maths::FMatrix4>(name);
			case SAMPLER_2D: return std::make_any<Resources::Texture*>(nullptr);
			case SAMPLER_CUBE: return std::make_any<Resources::Texture*>(nullptr);
			case SAMPLER_BUFFER: return std::make_any<Resources::Texture*>(nullptr);
			case SAMPLER_2DARRAY: return std::make_any<Resources::Texture*>(nullptr);
			case INTSAMPLER_BUFFER: return std::make_any<Resources::Texture*>(nullptr);
			case UINTSAMPLER_BUFFER: return std::make_any<Resources::Texture*>(nullptr);
			default: return std::nullopt;
			}
			}();

			// Only add the uniform if it has a value (unsupported uniform types will be ignored)
			if (uniformValue.has_value())
			{
				m_context.uniforms.emplace(name, Settings::UniformInfo{
					.type = uniformType,
					.name = name,
					.defaultValue = uniformValue
					});
			}
	}
}

template<>
Tools::Utils::OptRef<const Rendering::Settings::UniformInfo> Rendering::HAL::GLShaderProgram::GetUniformInfo(const std::string& p_name) const
{
	if (m_context.uniforms.contains(p_name))
	{
		return m_context.uniforms.at(p_name);
	}

	return std::nullopt;
}

template<>
const std::unordered_map<std::string, Rendering::Settings::UniformInfo>& Rendering::HAL::GLShaderProgram::GetUniforms() const
{
	return m_context.uniforms;
}
