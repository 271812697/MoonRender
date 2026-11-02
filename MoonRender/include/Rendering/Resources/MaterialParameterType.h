#pragma once
#include <cstdint>
#include <string_view>

/** The material DSL's parameter types, shared by the compiler and the runtime.
 *
 * The package stores these as a uint8 (filament stores UniformType / SamplerType the same
 * way); the two spellings (the DSL name and the GLSL name) are produced only when a package
 * dump or a GLSL declaration needs them. Keeping the enum in a header both sides include is
 * what stops the package from carrying type names as strings. */
namespace Moon::Material
{
	enum class ParameterType : uint8_t
	{
		Bool,
		Bool2,
		Bool3,
		Bool4,
		Int,
		Int2,
		Int3,
		Int4,
		UInt,
		UInt2,
		UInt3,
		UInt4,
		Float,
		Float2,
		Float3,
		Float4,
		Mat2,
		Mat3,
		Mat4,
		Sampler2D,
		Sampler2DArray,
		Sampler3D,
		SamplerCube,
		SamplerBuffer,
		ISamplerBuffer,
		USamplerBuffer,
		Unknown,
	};

	/** The 1x1 texture the runtime binds when a sampler slot has no material texture. */
	enum class SamplerFallback : uint8_t
	{
		Default = 0,
		FlatNormal = 1,
	};

	constexpr bool IsSamplerType(ParameterType p_type) noexcept
	{
		switch (p_type)
		{
		case ParameterType::Sampler2D:
		case ParameterType::Sampler2DArray:
		case ParameterType::Sampler3D:
		case ParameterType::SamplerCube:
		case ParameterType::SamplerBuffer:
		case ParameterType::ISamplerBuffer:
		case ParameterType::USamplerBuffer:
			return true;
		default:
			return false;
		}
	}

	constexpr const char* ParameterTypeName(ParameterType p_type) noexcept
	{
		switch (p_type)
		{
		case ParameterType::Bool: return "bool";
		case ParameterType::Bool2: return "bool2";
		case ParameterType::Bool3: return "bool3";
		case ParameterType::Bool4: return "bool4";
		case ParameterType::Int: return "int";
		case ParameterType::Int2: return "int2";
		case ParameterType::Int3: return "int3";
		case ParameterType::Int4: return "int4";
		case ParameterType::UInt: return "uint";
		case ParameterType::UInt2: return "uint2";
		case ParameterType::UInt3: return "uint3";
		case ParameterType::UInt4: return "uint4";
		case ParameterType::Float: return "float";
		case ParameterType::Float2: return "float2";
		case ParameterType::Float3: return "float3";
		case ParameterType::Float4: return "float4";
		case ParameterType::Mat2: return "mat2";
		case ParameterType::Mat3: return "mat3";
		case ParameterType::Mat4: return "mat4";
		case ParameterType::Sampler2D: return "sampler2D";
		case ParameterType::Sampler2DArray: return "sampler2DArray";
		case ParameterType::Sampler3D: return "sampler3D";
		case ParameterType::SamplerCube: return "samplerCube";
		case ParameterType::SamplerBuffer: return "samplerBuffer";
		case ParameterType::ISamplerBuffer: return "isamplerBuffer";
		case ParameterType::USamplerBuffer: return "usamplerBuffer";
		default: return "unknown";
		}
	}

	constexpr const char* ParameterTypeGlsl(ParameterType p_type) noexcept
	{
		switch (p_type)
		{
		case ParameterType::Bool: return "bool";
		case ParameterType::Bool2: return "bvec2";
		case ParameterType::Bool3: return "bvec3";
		case ParameterType::Bool4: return "bvec4";
		case ParameterType::Int: return "int";
		case ParameterType::Int2: return "ivec2";
		case ParameterType::Int3: return "ivec3";
		case ParameterType::Int4: return "ivec4";
		case ParameterType::UInt: return "uint";
		case ParameterType::UInt2: return "uvec2";
		case ParameterType::UInt3: return "uvec3";
		case ParameterType::UInt4: return "uvec4";
		case ParameterType::Float: return "float";
		case ParameterType::Float2: return "vec2";
		case ParameterType::Float3: return "vec3";
		case ParameterType::Float4: return "vec4";
		case ParameterType::Mat2: return "mat2";
		case ParameterType::Mat3: return "mat3";
		case ParameterType::Mat4: return "mat4";
		case ParameterType::Sampler2D: return "sampler2D";
		case ParameterType::Sampler2DArray: return "sampler2DArray";
		case ParameterType::Sampler3D: return "sampler3D";
		case ParameterType::SamplerCube: return "samplerCube";
		case ParameterType::SamplerBuffer: return "samplerBuffer";
		case ParameterType::ISamplerBuffer: return "isamplerBuffer";
		case ParameterType::USamplerBuffer: return "usamplerBuffer";
		default: return "unknown";
		}
	}

	inline ParameterType ParseParameterType(std::string_view p_name) noexcept
	{
		for (const ParameterType type : {
			ParameterType::Bool, ParameterType::Bool2, ParameterType::Bool3, ParameterType::Bool4,
			ParameterType::Int, ParameterType::Int2, ParameterType::Int3, ParameterType::Int4,
			ParameterType::UInt, ParameterType::UInt2, ParameterType::UInt3, ParameterType::UInt4,
			ParameterType::Float, ParameterType::Float2, ParameterType::Float3, ParameterType::Float4,
			ParameterType::Mat2, ParameterType::Mat3, ParameterType::Mat4,
			ParameterType::Sampler2D, ParameterType::Sampler2DArray, ParameterType::Sampler3D,
			ParameterType::SamplerCube, ParameterType::SamplerBuffer,
			ParameterType::ISamplerBuffer, ParameterType::USamplerBuffer })
		{
			if (p_name == ParameterTypeName(type))
			{
				return type;
			}
		}
		return ParameterType::Unknown;
	}

	inline SamplerFallback ParseSamplerFallback(std::string_view p_name) noexcept
	{
		return p_name == "flatNormal" ? SamplerFallback::FlatNormal : SamplerFallback::Default;
	}

	constexpr const char* SamplerFallbackName(SamplerFallback p_fallback) noexcept
	{
		return p_fallback == SamplerFallback::FlatNormal ? "flatNormal" : "";
	}
}
