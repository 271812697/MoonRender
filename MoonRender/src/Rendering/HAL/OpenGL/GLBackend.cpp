#pragma once
#include <format>
#include <glad/glad.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <iostream>
// Needs to be included after OpenGL headers
#include <tracy/TracyOpenGL.hpp>

#include <Rendering/HAL/OpenGL/GLBackend.h>
#include <Rendering/HAL/OpenGL/GLTypes.h>
#include <Rendering/Utils/Conversions.h>

namespace
{
	void GLDebugMessageCallback(uint32_t source, uint32_t type, uint32_t id, uint32_t severity, int32_t length, const char* message, const void* userParam)
	{
		// ignore non-significant error/warning codes
		if (id == 131169 || id == 131185 || id == 131218 || id == 131204) return;

		std::string output;

		output += "OpenGL Debug Message:\n";
		output += "Debug message (" + std::to_string(id) + "): " + message + "\n";

		switch (source)
		{
		case GL_DEBUG_SOURCE_API:				output += "Source: API";				break;
		case GL_DEBUG_SOURCE_WINDOW_SYSTEM:		output += "Source: Window System";		break;
		case GL_DEBUG_SOURCE_SHADER_COMPILER:	output += "Source: Shader Compiler";	break;
		case GL_DEBUG_SOURCE_THIRD_PARTY:		output += "Source: Third Party";		break;
		case GL_DEBUG_SOURCE_APPLICATION:		output += "Source: Application";		break;
		case GL_DEBUG_SOURCE_OTHER:				output += "Source: Other";				break;
		}

		output += "\n";

		switch (type)
		{
		case GL_DEBUG_TYPE_ERROR:               output += "Type: Error";				break;
		case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: output += "Type: Deprecated Behaviour"; break;
		case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:  output += "Type: Undefined Behaviour";	break;
		case GL_DEBUG_TYPE_PORTABILITY:         output += "Type: Portability";			break;
		case GL_DEBUG_TYPE_PERFORMANCE:         output += "Type: Performance";			break;
		case GL_DEBUG_TYPE_MARKER:              output += "Type: Marker";				break;
		case GL_DEBUG_TYPE_PUSH_GROUP:          output += "Type: Push Group";			break;
		case GL_DEBUG_TYPE_POP_GROUP:           output += "Type: Pop Group";			break;
		case GL_DEBUG_TYPE_OTHER:               output += "Type: Other";				break;
		}

		output += "\n";

		switch (severity)
		{
		case GL_DEBUG_SEVERITY_HIGH:			output += "Severity: High";				break;
		case GL_DEBUG_SEVERITY_MEDIUM:			output += "Severity: Medium";			break;
		case GL_DEBUG_SEVERITY_LOW:				output += "Severity: Low";				break;
		case GL_DEBUG_SEVERITY_NOTIFICATION:	output += "Severity: Notification";		break;
		}

		switch (severity)
		{
		case GL_DEBUG_SEVERITY_HIGH:			std::cout << (output) << std::endl;;	break;
		case GL_DEBUG_SEVERITY_MEDIUM:			std::cout << (output) << std::endl;;	break;
		case GL_DEBUG_SEVERITY_LOW:				std::cout << (output) << std::endl;;		break;
		case GL_DEBUG_SEVERITY_NOTIFICATION:	std::cout << (output) << std::endl;;		break;
		}
	}

	bool GetBool(uint32_t p_parameter)
	{
		GLboolean result;
		glGetBooleanv(p_parameter, &result);
		return static_cast<bool>(result);
	}

	bool GetBool(uint32_t p_parameter, uint32_t p_index)
	{
		GLboolean result;
		glGetBooleani_v(p_parameter, p_index, &result);
		return static_cast<bool>(result);
	}

	int GetInt(uint32_t p_parameter)
	{
		GLint result[4];
		glGetIntegerv(p_parameter, result);
		return static_cast<int>(result[0]);
	}

	int GetInt(uint32_t p_parameter, uint32_t p_index)
	{
		GLint result[4];
		glGetIntegeri_v(p_parameter, p_index, result);
		return static_cast<int>(result[0]);
	}

	float GetFloat(uint32_t p_parameter)
	{
		GLfloat result;
		glGetFloatv(p_parameter, &result);
		return static_cast<float>(result);
	}

	float GetFloat(uint32_t p_parameter, uint32_t p_index)
	{
		GLfloat result;
		glGetFloati_v(p_parameter, p_index, &result);
		return static_cast<float>(result);
	}

	double GetDouble(uint32_t p_parameter)
	{
		GLdouble result;
		glGetDoublev(p_parameter, &result);
		return static_cast<double>(result);
	}

	double GetDouble(uint32_t p_parameter, uint32_t p_index)
	{
		GLdouble result;
		glGetDoublei_v(p_parameter, p_index, &result);
		return static_cast<double>(result);
	}

	int64_t GetInt64(uint32_t p_parameter)
	{
		GLint64 result;
		glGetInteger64v(p_parameter, &result);
		return static_cast<int64_t>(result);
	}

	int64_t GetInt64(uint32_t p_parameter, uint32_t p_index)
	{
		GLint64 result;
		glGetInteger64i_v(p_parameter, p_index, &result);
		return static_cast<int64_t>(result);
	}

	std::string GetString(uint32_t p_parameter)
	{
		const GLubyte* result = glGetString(p_parameter);
		return result ? reinterpret_cast<const char*>(result) : std::string();
	}

	std::string GetString(uint32_t p_parameter, uint32_t p_index)
	{
		const GLubyte* result = glGetStringi(p_parameter, p_index);
		return result ? reinterpret_cast<const char*>(result) : std::string();
	}

	/**
	* Very expensive! Call it once, and make sure you always keep track of state changes
	*/
	Rendering::Data::PipelineState RetrieveOpenGLPipelineState()
	{
		using namespace Rendering::Settings;
		using namespace Rendering::HAL;

		Rendering::Data::PipelineState pso;

		// Rasterization
		pso.rasterizationMode = static_cast<ERasterizationMode>(GetInt(GL_POLYGON_MODE));
		pso.lineWidthPow2 = Rendering::Utils::Conversions::FloatToPow2(GetFloat(GL_LINE_WIDTH));

		// Color write mask
		GLboolean colorWriteMask[4];
		glGetBooleanv(GL_COLOR_WRITEMASK, colorWriteMask);
		pso.colorWriting.r = colorWriteMask[0];
		pso.colorWriting.g = colorWriteMask[1];
		pso.colorWriting.b = colorWriteMask[2];
		pso.colorWriting.a = colorWriteMask[3];

		// Capability
		pso.depthWriting = GetBool(GL_DEPTH_WRITEMASK);
		pso.blending = GetBool(GL_BLEND);
		pso.culling = GetBool(GL_CULL_FACE);
		pso.dither = GetBool(GL_DITHER);
		pso.polygonOffsetFill = GetBool(GL_POLYGON_OFFSET_FILL);
		pso.polygonOffsetLine = GetBool(GL_POLYGON_OFFSET_LINE);

		pso.sampleAlphaToCoverage = GetBool(GL_SAMPLE_ALPHA_TO_COVERAGE);
		pso.depthTest = GetBool(GL_DEPTH_TEST);
		pso.scissorTest = GetBool(GL_SCISSOR_TEST);
		pso.stencilTest = GetBool(GL_STENCIL_TEST);
		pso.multisample = GetBool(GL_MULTISAMPLE);

		// Stencil
		pso.stencilFuncOp = ValueToEnum<EComparaisonAlgorithm>(static_cast<GLenum>(GetInt(GL_STENCIL_FUNC)));
		pso.stencilFuncRef = GetInt(GL_STENCIL_REF);
		pso.stencilFuncMask = static_cast<uint32_t>(GetInt(GL_STENCIL_VALUE_MASK));

		pso.stencilWriteMask = static_cast<uint32_t>(GetInt(GL_STENCIL_WRITEMASK));

		pso.stencilOpFail = ValueToEnum<EOperation>(static_cast<GLenum>(GetInt(GL_STENCIL_FAIL)));
		pso.depthOpFail = ValueToEnum<EOperation>(static_cast<GLenum>(GetInt(GL_STENCIL_PASS_DEPTH_FAIL)));
		pso.bothOpFail = ValueToEnum<EOperation>(static_cast<GLenum>(GetInt(GL_STENCIL_PASS_DEPTH_PASS)));

		// Depth
		pso.depthFunc = ValueToEnum<EComparaisonAlgorithm>(static_cast<GLenum>(GetInt(GL_DEPTH_FUNC)));

		// Culling
		pso.cullFace = ValueToEnum<ECullFace>(static_cast<GLenum>(GetInt(GL_CULL_FACE_MODE)));

		// Blending
		pso.blendingSrcFactor = ValueToEnum<EBlendingFactor>(static_cast<GLenum>(GetInt(GL_BLEND_SRC)));
		pso.blendingDestFactor = ValueToEnum<EBlendingFactor>(static_cast<GLenum>(GetInt(GL_BLEND_DST)));
		pso.blendingEquation = ValueToEnum<EBlendingEquation>(static_cast<GLenum>(GetInt(GL_BLEND_EQUATION)));

		return pso;
	}
}

namespace Rendering::HAL
{
	template<>
	std::optional<Data::PipelineState> GLBackend::Init(bool debug)
	{
		// Load once. glad's table is process-wide, and a second load from another context (the
		// known-good context with whatever that call returns - including null for entry points the
		// loader cannot resolve there. The addresses are the same for every context of the same
		// driver, so the first load is the one to keep.
		static const bool gladReady = [] {
			const int loaded = gladLoadGL();
			if (loaded == 0)
			{
				std::cout << ("GLAD failed to initialize") << std::endl;
				return false;
			}
			return true;
		}();
		if (!gladReady)
		{
			return std::nullopt;
		}

		// Tracy's GPU context is thread-local: remember which thread made it, so the frame's Collect
		// only runs where that context exists (see OnFrameCompleted).
		m_context.initThreadId = std::this_thread::get_id();
		TracyGpuContext;

		if (debug)
		{
			glEnable(GL_DEBUG_OUTPUT);
			glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
			glDebugMessageCallback(GLDebugMessageCallback, nullptr);
		}

		// Seamless cubemap (always on)
		glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glCullFace(GL_BACK);

		// Reversed-Z depth: far maps to window depth 0 and near to 1. With the
		// float32 depth buffer this gives roughly uniform precision across the
		// whole near/far range (see docs/DepthPrecision.md). The depth test
		// direction is flipped accordingly (LESS -> GREATER).
		glDepthRange(1.0, 0.0);
		glClearDepth(0.0);

		if (std::getenv("MOON_FRAME_TRACE") != nullptr)
		{
			GLint depthFunc = 0;
			glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);
			std::fprintf(stderr, "[gl] init context=%s depthFunc=0x%04x\n",
				glGetString(GL_VERSION) != nullptr ? "current" : "NONE",
				static_cast<unsigned>(depthFunc));
			std::fflush(stderr);
		}

		return RetrieveOpenGLPipelineState();
	}

	template<>
	void GLBackend::OnFrameCompleted()
	{
		// Tracy's GPU context belongs to the thread that created it - Init, which runs on the
		// module's render thread when it has one. Collecting from anywhere else dereferences a null
		// context (that is the crash this guard removes): the zones are simply not collected from
		// the recording side.
		if (std::this_thread::get_id() != m_context.initThreadId)
		{
			return;
		}
		TracyGpuCollect;
	}

	template<>
	void GLBackend::Clear(bool p_colorBuffer, bool p_depthBuffer, bool p_stencilBuffer)
	{
		GLbitfield clearMask = 0;
		if (p_colorBuffer) clearMask |= GL_COLOR_BUFFER_BIT;
		if (p_depthBuffer) clearMask |= GL_DEPTH_BUFFER_BIT;
		if (p_stencilBuffer) clearMask |= GL_STENCIL_BUFFER_BIT;

		if (clearMask != 0)
		{
			glClear(clearMask);
		}
	}

	template<>
	void GLBackend::DrawElements(Settings::EPrimitiveMode p_primitiveMode, uint32_t p_indexCount)
	{
		glDrawElements(EnumToValue<GLenum>(p_primitiveMode), p_indexCount, GL_UNSIGNED_INT, nullptr);
	}

	template<>
	void GLBackend::DrawElementsInstanced(Settings::EPrimitiveMode p_primitiveMode, uint32_t p_indexCount, uint32_t p_instances)
	{
		glDrawElementsInstanced(EnumToValue<GLenum>(p_primitiveMode), p_indexCount, GL_UNSIGNED_INT, nullptr, p_instances);
	}

	template<>
	void GLBackend::DrawArrays(Settings::EPrimitiveMode p_primitiveMode, uint32_t p_vertexCount)
	{
		glDrawArrays(EnumToValue<GLenum>(p_primitiveMode), 0, p_vertexCount);
	}

	template<>
	void GLBackend::DrawArraysInstanced(Settings::EPrimitiveMode p_primitiveMode, uint32_t p_vertexCount, uint32_t p_instances)
	{
		glDrawArraysInstanced(EnumToValue<GLenum>(p_primitiveMode), 0, p_vertexCount, p_instances);
	}

	template<>
	void GLBackend::BindVertexArray(uint32_t p_vertexArray)
	{
		glBindVertexArray(p_vertexArray);
	}

	template<>
	void GLBackend::BindFramebuffer(uint32_t p_framebuffer)
	{
		glBindFramebuffer(GL_FRAMEBUFFER, p_framebuffer);

		// MOON_FRAME_TRACE: the HAL's own binds (Framebuffer::Bind and friends) bypass the driver
		// command, so they are checked here too - otherwise an incomplete target bound this way is
		// invisible in the trace and only shows up as GL_INVALID_FRAMEBUFFER_OPERATION later.
		if (p_framebuffer != 0 && std::getenv("MOON_FRAME_TRACE") != nullptr)
		{
			const GLenum status = glCheckNamedFramebufferStatus(p_framebuffer, GL_FRAMEBUFFER);
			std::fprintf(stderr, "[fbo] hal bind %u status=0x%04x\n", p_framebuffer, status);
			std::fflush(stderr);
		}
	}

	template<>
	void GLBackend::SetClearColor(float p_red, float p_green, float p_blue, float p_alpha)
	{
		glClearColor(p_red, p_green, p_blue, p_alpha);
	}

	template<>
	void GLBackend::SetRasterizationLinesWidth(float p_width)
	{
		glLineWidth(p_width);
	}

	template<>
	void GLBackend::SetRasterizationMode(Settings::ERasterizationMode p_rasterizationMode)
	{
		glPolygonMode(GL_FRONT_AND_BACK, EnumToValue<GLenum>(p_rasterizationMode));
	}

	template<>
	void GLBackend::SetCapability(Settings::ERenderingCapability p_capability, bool p_value)
	{
		(p_value ? glEnable : glDisable)(EnumToValue<GLenum>(p_capability));
	}

	template<>
	bool GLBackend::GetCapability(Settings::ERenderingCapability p_capability)
	{
		return glIsEnabled(EnumToValue<GLenum>(p_capability));
	}

	template<>
	void GLBackend::SetStencilAlgorithm(Settings::EComparaisonAlgorithm p_algorithm, int32_t p_reference, uint32_t p_mask)
	{
		glStencilFunc(EnumToValue<GLenum>(p_algorithm), p_reference, p_mask);
	}

	template<>
	void GLBackend::SetDepthAlgorithm(Settings::EComparaisonAlgorithm p_algorithm)
	{
		glDepthFunc(EnumToValue<GLenum>(p_algorithm));

		// MOON_FRAME_TRACE: the depth comparison is the one piece of state whose reversal
		// (reversed-Z, glDepthRange(1,0)) makes a wrong value silently discard every fragment -
		// so every change is logged with what GL ended up in, and whether a context was current.
		if (std::getenv("MOON_FRAME_TRACE") != nullptr)
		{
			GLint actual = 0;
			glGetIntegerv(GL_DEPTH_FUNC, &actual);
			std::fprintf(stderr, "[gl] depthFunc wanted=0x%04x actual=0x%04x context=%s\n",
				EnumToValue<GLenum>(p_algorithm), static_cast<unsigned>(actual),
				glGetString(GL_VERSION) != nullptr ? "current" : "NONE");
			std::fflush(stderr);
		}
	}

	template<>
	void GLBackend::SetStencilMask(uint32_t p_mask)
	{
		glStencilMask(p_mask);
	}

	template<>
	void GLBackend::SetStencilOperations(Settings::EOperation p_stencilFail, Settings::EOperation p_depthFail, Settings::EOperation p_bothPass)
	{
		glStencilOp(EnumToValue<GLenum>(p_stencilFail), EnumToValue<GLenum>(p_depthFail), EnumToValue<GLenum>(p_bothPass));
	}

	template<>
	void GLBackend::SetBlendingFunction(Settings::EBlendingFactor p_sourceFactor, Settings::EBlendingFactor p_destinationFactor)
	{
		glBlendFunc(
			EnumToValue<GLenum>(p_sourceFactor),
			EnumToValue<GLenum>(p_destinationFactor)
		);
	}
	template<>
	void GLBackend::PolygonOffset(float factor, float uints)
	{
		glPolygonOffset(factor, uints);
	}

	template<>
	void GLBackend::SetBlendingEquation(Settings::EBlendingEquation p_equation)
	{
		glBlendEquation(EnumToValue<GLenum>(p_equation));
	}

	template<>
	void GLBackend::SetCullFace(Settings::ECullFace p_cullFace)
	{
		glCullFace(EnumToValue<GLenum>(p_cullFace));
	}

	template<>
	void GLBackend::SetDepthWriting(bool p_enable)
	{
		glDepthMask(p_enable);
	}

	template<>
	void GLBackend::SetColorWriting(bool p_enableRed, bool p_enableGreen, bool p_enableBlue, bool p_enableAlpha)
	{
		glColorMask(p_enableRed, p_enableGreen, p_enableBlue, p_enableAlpha);
	}

	template<>
	void GLBackend::SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
	{
		glViewport(x, y, width, height);
	}

	template<>
	void GLBackend::SetScissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
	{
		glScissor(x, y, width, height);
	}

	template<>
	std::string GLBackend::GetVendor()
	{
		return GetString(GL_VENDOR);
	}

	template<>
	std::string GLBackend::GetHardware()
	{
		return GetString(GL_RENDERER);
	}

	template<>
	std::string GLBackend::GetVersion()
	{
		return GetString(GL_VERSION);
	}

	template<>
	std::string GLBackend::GetShadingLanguageVersion()
	{
		return GetString(GL_SHADING_LANGUAGE_VERSION);
	}
}
