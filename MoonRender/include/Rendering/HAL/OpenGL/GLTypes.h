#pragma once
#include <glad/glad.h>
#include <Rendering/Settings/EAccessSpecifier.h>
#include <Rendering/Settings/EBlendingEquation.h>
#include <Rendering/Settings/EBlendingFactor.h>
#include <Rendering/Settings/EBufferType.h>
#include <Rendering/Settings/EComparaisonAlgorithm.h>
#include <Rendering/Settings/ECullFace.h>
#include <Rendering/Settings/EDataType.h>
#include <Rendering/Settings/EFormat.h>
#include <Rendering/Settings/EFramebufferAttachment.h>
#include <Rendering/Settings/EInternalFormat.h>
#include <Rendering/Settings/EOperation.h>
#include <Rendering/Settings/EPrimitiveMode.h>
#include <Rendering/Settings/ERasterizationMode.h>
#include <Rendering/Settings/ERenderingCapability.h>
#include <Rendering/Settings/EShaderType.h>
#include <Rendering/Settings/ETextureFilteringMode.h>
#include <Rendering/Settings/ETextureType.h>
#include <Rendering/Settings/ETextureWrapMode.h>
#include <Rendering/Settings/EUniformType.h>
#include <Rendering/Settings/EPixelDataFormat.h>
#include <Rendering/Settings/EPixelDataType.h>
#include <Tools/Utils/EnumMapper.h>

namespace Rendering::HAL
{
	template <typename ValueType, typename EnumType>
	constexpr ValueType EnumToValue(EnumType enumValue)
	{
		return Tools::Utils::ToValueImpl<EnumType, ValueType>(enumValue);
	}

	template <typename EnumType, typename ValueType>
	constexpr EnumType ValueToEnum(ValueType value)
	{
		return Tools::Utils::FromValueImpl<EnumType, ValueType>(value);
	}
}

template <>
struct Tools::Utils::MappingFor<Rendering::Settings::EComparaisonAlgorithm, GLenum>
{
	using EnumType = Rendering::Settings::EComparaisonAlgorithm;
	using type = std::tuple<
		EnumValuePair<EnumType::NEVER, GL_NEVER>,
		EnumValuePair<EnumType::LESS, GL_LESS>,
		EnumValuePair<EnumType::EQUAL, GL_EQUAL>,
		EnumValuePair<EnumType::LESS_EQUAL, GL_LEQUAL>,
		EnumValuePair<EnumType::GREATER, GL_GREATER>,
		EnumValuePair<EnumType::NOTEQUAL, GL_NOTEQUAL>,
		EnumValuePair<EnumType::GREATER_EQUAL, GL_GEQUAL>,
		EnumValuePair<EnumType::ALWAYS, GL_ALWAYS>
	>;
};

template <>
struct Tools::Utils::MappingFor<Rendering::Settings::EBlendingEquation, GLenum>
{
	using EnumType = Rendering::Settings::EBlendingEquation;
	using type = std::tuple<
		EnumValuePair<EnumType::FUNC_ADD, GL_FUNC_ADD>,
		EnumValuePair<EnumType::FUNC_SUBTRACT, GL_FUNC_SUBTRACT>,
		EnumValuePair<EnumType::FUNC_REVERSE_SUBTRACT, GL_FUNC_REVERSE_SUBTRACT>,
		EnumValuePair<EnumType::MIN, GL_MIN>,
		EnumValuePair<EnumType::MAX, GL_MAX>
	>;
};

template <>
struct Tools::Utils::MappingFor<Rendering::Settings::EBlendingFactor, GLenum>
{
	using EnumType = Rendering::Settings::EBlendingFactor;
	using type = std::tuple<
		EnumValuePair<EnumType::ZERO, GL_ZERO>,
		EnumValuePair<EnumType::ONE, GL_ONE>,
		EnumValuePair<EnumType::SRC_COLOR, GL_SRC_COLOR>,
		EnumValuePair<EnumType::ONE_MINUS_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR>,
		EnumValuePair<EnumType::DST_COLOR, GL_DST_COLOR>,
		EnumValuePair<EnumType::ONE_MINUS_DST_COLOR, GL_ONE_MINUS_DST_COLOR>,
		EnumValuePair<EnumType::SRC_ALPHA, GL_SRC_ALPHA>,
		EnumValuePair<EnumType::ONE_MINUS_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA>,
		EnumValuePair<EnumType::DST_ALPHA, GL_DST_ALPHA>,
		EnumValuePair<EnumType::ONE_MINUS_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA>,
		EnumValuePair<EnumType::CONSTANT_COLOR, GL_CONSTANT_COLOR>,
		EnumValuePair<EnumType::ONE_MINUS_CONSTANT_COLOR, GL_ONE_MINUS_CONSTANT_COLOR>,
		EnumValuePair<EnumType::CONSTANT_ALPHA, GL_CONSTANT_ALPHA>,
		EnumValuePair<EnumType::ONE_MINUS_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA>,
		EnumValuePair<EnumType::SRC_ALPHA_SATURATE, GL_SRC_ALPHA_SATURATE>,
		EnumValuePair<EnumType::SRC1_COLOR, GL_SRC1_COLOR>,
		EnumValuePair<EnumType::ONE_MINUS_SRC1_COLOR, GL_ONE_MINUS_SRC1_COLOR>,
		EnumValuePair<EnumType::SRC1_ALPHA, GL_SRC1_ALPHA>,
		EnumValuePair<EnumType::ONE_MINUS_SRC1_ALPHA, GL_ONE_MINUS_SRC1_ALPHA>
	>;
};

template <>
struct Tools::Utils::MappingFor<Rendering::Settings::ERasterizationMode, GLenum>
{
	using EnumType = Rendering::Settings::ERasterizationMode;
	using type = std::tuple<
		EnumValuePair<EnumType::POINT, GL_POINT>,
		EnumValuePair<EnumType::LINE, GL_LINE>,
		EnumValuePair<EnumType::FILL, GL_FILL>
	>;
};

template <>
struct Tools::Utils::MappingFor<Rendering::Settings::ECullFace, GLenum>
{
	using EnumType = Rendering::Settings::ECullFace;
	using type = std::tuple<
		EnumValuePair<EnumType::FRONT, GL_FRONT>,
		EnumValuePair<EnumType::BACK, GL_BACK>,
		EnumValuePair<EnumType::FRONT_AND_BACK, GL_FRONT_AND_BACK>
	>;
};

template <>
struct Tools::Utils::MappingFor<Rendering::Settings::EOperation, GLenum>
{
	using EnumType = Rendering::Settings::EOperation;
	using type = std::tuple<
		EnumValuePair<EnumType::KEEP, GL_KEEP>,
		EnumValuePair<EnumType::ZERO, GL_ZERO>,
		EnumValuePair<EnumType::REPLACE, GL_REPLACE>,
		EnumValuePair<EnumType::INCREMENT, GL_INCR>,
		EnumValuePair<EnumType::INCREMENT_WRAP, GL_INCR_WRAP>,
		EnumValuePair<EnumType::DECREMENT, GL_DECR>,
		EnumValuePair<EnumType::DECREMENT_WRAP, GL_DECR_WRAP>,
		EnumValuePair<EnumType::INVERT, GL_INVERT>
	>;
};

template <>
struct Tools::Utils::MappingFor<Rendering::Settings::ERenderingCapability, GLenum>
{
	using EnumType = Rendering::Settings::ERenderingCapability;
	using type = std::tuple<
		EnumValuePair<EnumType::BLEND, GL_BLEND>,
		EnumValuePair<EnumType::CULL_FACE, GL_CULL_FACE>,
		EnumValuePair<EnumType::DEPTH_TEST, GL_DEPTH_TEST>,
		EnumValuePair<EnumType::DITHER, GL_DITHER>,
		EnumValuePair<EnumType::POLYGON_OFFSET_FILL, GL_POLYGON_OFFSET_FILL>,
		EnumValuePair<EnumType::POLYGON_OFFSET_LINE, GL_POLYGON_OFFSET_LINE>,
		EnumValuePair<EnumType::SAMPLE_ALPHA_TO_COVERAGE, GL_SAMPLE_ALPHA_TO_COVERAGE>,
		EnumValuePair<EnumType::SAMPLE_COVERAGE, GL_SAMPLE_COVERAGE>,
		EnumValuePair<EnumType::SCISSOR_TEST, GL_SCISSOR_TEST>,
		EnumValuePair<EnumType::STENCIL_TEST, GL_STENCIL_TEST>,
		EnumValuePair<EnumType::MULTISAMPLE, GL_MULTISAMPLE>
	>;
};

template <>
struct Tools::Utils::MappingFor<Rendering::Settings::EPrimitiveMode, GLenum>
{
	using EnumType = Rendering::Settings::EPrimitiveMode;
	using type = std::tuple<
		EnumValuePair<EnumType::POINTS, GL_POINTS>,
		EnumValuePair<EnumType::LINES, GL_LINES>,
		EnumValuePair<EnumType::LINE_LOOP, GL_LINE_LOOP>,
		EnumValuePair<EnumType::LINE_STRIP, GL_LINE_STRIP>,
		EnumValuePair<EnumType::TRIANGLES, GL_TRIANGLES>,
		EnumValuePair<EnumType::TRIANGLE_STRIP, GL_TRIANGLE_STRIP>,
		EnumValuePair<EnumType::TRIANGLE_FAN, GL_TRIANGLE_FAN>,
		EnumValuePair<EnumType::LINES_ADJACENCY, GL_LINES_ADJACENCY>,
		EnumValuePair<EnumType::LINE_STRIP_ADJACENCY, GL_LINE_STRIP_ADJACENCY>,
		EnumValuePair<EnumType::PATCHES, GL_PATCHES>
	>;
};

template <>
struct Tools::Utils::MappingFor<Rendering::Settings::EFormat, GLenum>
{
	using EnumType = Rendering::Settings::EFormat;
	using type = std::tuple<
		EnumValuePair<EnumType::RED, GL_RED>,
		EnumValuePair<EnumType::RG, GL_RG>,
		EnumValuePair<EnumType::RGB, GL_RGB>,
		EnumValuePair<EnumType::BGR, GL_BGR>,
		EnumValuePair<EnumType::RGBA, GL_RGBA>,
		EnumValuePair<EnumType::BGRA, GL_BGRA>,
		EnumValuePair<EnumType::RED_INTEGER, GL_RED_INTEGER>,
		EnumValuePair<EnumType::RG_INTEGER, GL_RG_INTEGER>,
		EnumValuePair<EnumType::RGB_INTEGER, GL_RGB_INTEGER>,
		EnumValuePair<EnumType::BGR_INTEGER, GL_BGR_INTEGER>,
		EnumValuePair<EnumType::RGBA_INTEGER, GL_RGBA_INTEGER>,
		EnumValuePair<EnumType::BGRA_INTEGER, GL_BGRA_INTEGER>,
		EnumValuePair<EnumType::STENCIL_INDEX, GL_STENCIL_INDEX>,
		EnumValuePair<EnumType::DEPTH_COMPONENT, GL_DEPTH_COMPONENT>,
		EnumValuePair<EnumType::DEPTH_STENCIL, GL_DEPTH_STENCIL>
	>;
};

template <>
struct Tools::Utils::MappingFor<Rendering::Settings::ETextureFilteringMode, GLenum>
{
	using EnumType = Rendering::Settings::ETextureFilteringMode;
	using type = std::tuple<
		EnumValuePair<EnumType::NEAREST, GL_NEAREST>,
		EnumValuePair<EnumType::LINEAR, GL_LINEAR>,
		EnumValuePair<EnumType::NEAREST_MIPMAP_NEAREST, GL_NEAREST_MIPMAP_NEAREST>,
		EnumValuePair<EnumType::LINEAR_MIPMAP_LINEAR, GL_LINEAR_MIPMAP_LINEAR>,
		EnumValuePair<EnumType::LINEAR_MIPMAP_NEAREST, GL_LINEAR_MIPMAP_NEAREST>,
		EnumValuePair<EnumType::NEAREST_MIPMAP_LINEAR, GL_NEAREST_MIPMAP_LINEAR>
	>;
};

template <>
struct Tools::Utils::MappingFor<Rendering::Settings::ETextureWrapMode, GLenum>
{
	using EnumType = Rendering::Settings::ETextureWrapMode;
	using type = std::tuple<
		EnumValuePair<EnumType::REPEAT, GL_REPEAT>,
		EnumValuePair<EnumType::CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE>,
		EnumValuePair<EnumType::CLAMP_TO_BORDER, GL_CLAMP_TO_BORDER>,
		EnumValuePair<EnumType::MIRRORED_REPEAT, GL_MIRRORED_REPEAT>,
		EnumValuePair<EnumType::MIRROR_CLAMP_TO_EDGE, GL_MIRROR_CLAMP_TO_EDGE>
	>;
};

template <>
struct Tools::Utils::MappingFor<Rendering::Settings::EPixelDataFormat, GLenum>
{
	using EnumType = Rendering::Settings::EPixelDataFormat;
	using type = std::tuple<
		EnumValuePair<EnumType::STENCIL_INDEX, GL_STENCIL_INDEX>,
		EnumValuePair<EnumType::DEPTH_COMPONENT, GL_DEPTH_COMPONENT>,
		EnumValuePair<EnumType::RED, GL_RED>,
		EnumValuePair<EnumType::GREEN, GL_GREEN>,
		EnumValuePair<EnumType::BLUE, GL_BLUE>,
		EnumValuePair<EnumType::ALPHA, GL_ALPHA>,
		EnumValuePair<EnumType::RGB, GL_RGB>,
		EnumValuePair<EnumType::BGR, GL_BGR>,
		EnumValuePair<EnumType::RGBA, GL_RGBA>,
		EnumValuePair<EnumType::BGRA, GL_BGRA>
	>;
};

template <>
struct Tools::Utils::MappingFor<Rendering::Settings::EPixelDataType, GLenum>
{
	using EnumType = Rendering::Settings::EPixelDataType;
	using type = std::tuple<
		EnumValuePair<EnumType::BYTE, GL_BYTE>,
		EnumValuePair<EnumType::UNSIGNED_BYTE, GL_UNSIGNED_BYTE>,
		EnumValuePair<EnumType::SHORT, GL_SHORT>,
		EnumValuePair<EnumType::UNSIGNED_SHORT, GL_UNSIGNED_SHORT>,
		EnumValuePair<EnumType::INT, GL_INT>,
		EnumValuePair<EnumType::UNSIGNED_INT, GL_UNSIGNED_INT>,
		EnumValuePair<EnumType::FLOAT, GL_FLOAT>,
		EnumValuePair<EnumType::UNSIGNED_BYTE_3_3_2, GL_UNSIGNED_BYTE_3_3_2>,
		EnumValuePair<EnumType::UNSIGNED_BYTE_2_3_3_REV, GL_UNSIGNED_BYTE_2_3_3_REV>,
		EnumValuePair<EnumType::UNSIGNED_SHORT_5_6_5, GL_UNSIGNED_SHORT_5_6_5>,
		EnumValuePair<EnumType::UNSIGNED_SHORT_5_6_5_REV, GL_UNSIGNED_SHORT_5_6_5_REV>,
		EnumValuePair<EnumType::UNSIGNED_SHORT_4_4_4_4, GL_UNSIGNED_SHORT_4_4_4_4>,
		EnumValuePair<EnumType::UNSIGNED_SHORT_4_4_4_4_REV, GL_UNSIGNED_SHORT_4_4_4_4_REV>,
		EnumValuePair<EnumType::UNSIGNED_SHORT_5_5_5_1, GL_UNSIGNED_SHORT_5_5_5_1>,
		EnumValuePair<EnumType::UNSIGNED_SHORT_1_5_5_5_REV, GL_UNSIGNED_SHORT_1_5_5_5_REV>,
		EnumValuePair<EnumType::UNSIGNED_INT_8_8_8_8, GL_UNSIGNED_INT_8_8_8_8>,
		EnumValuePair<EnumType::UNSIGNED_INT_8_8_8_8_REV, GL_UNSIGNED_INT_8_8_8_8_REV>,
		EnumValuePair<EnumType::UNSIGNED_INT_10_10_10_2, GL_UNSIGNED_INT_10_10_10_2>,
		EnumValuePair<EnumType::UNSIGNED_INT_2_10_10_10_REV, GL_UNSIGNED_INT_2_10_10_10_REV>
	>;
};

template <>
struct Tools::Utils::MappingFor<Rendering::Settings::EDataType, GLenum>
{
	using EnumType = Rendering::Settings::EDataType;
	using type = std::tuple<
		EnumValuePair<EnumType::BYTE, GL_BYTE>,
		EnumValuePair<EnumType::UNSIGNED_BYTE, GL_UNSIGNED_BYTE>,
		EnumValuePair<EnumType::SHORT, GL_SHORT>,
		EnumValuePair<EnumType::UNSIGNED_SHORT, GL_UNSIGNED_SHORT>,
		EnumValuePair<EnumType::INT, GL_INT>,
		EnumValuePair<EnumType::UNSIGNED_INT, GL_UNSIGNED_INT>,
		EnumValuePair<EnumType::FLOAT, GL_FLOAT>,
		EnumValuePair<EnumType::DOUBLE, GL_DOUBLE>
	>;
};

template <>
struct Tools::Utils::MappingFor<Rendering::Settings::EAccessSpecifier, GLenum>
{
	using EnumType = Rendering::Settings::EAccessSpecifier;
	using type = std::tuple<
		EnumValuePair<EnumType::STREAM_DRAW, GL_STREAM_DRAW>,
		EnumValuePair<EnumType::STREAM_READ, GL_STREAM_READ>,
		EnumValuePair<EnumType::STREAM_COPY, GL_STREAM_COPY>,
		EnumValuePair<EnumType::DYNAMIC_DRAW, GL_DYNAMIC_DRAW>,
		EnumValuePair<EnumType::DYNAMIC_READ, GL_DYNAMIC_READ>,
		EnumValuePair<EnumType::DYNAMIC_COPY, GL_DYNAMIC_COPY>,
		EnumValuePair<EnumType::STATIC_DRAW, GL_STATIC_DRAW>,
		EnumValuePair<EnumType::STATIC_READ, GL_STATIC_READ>,
		EnumValuePair<EnumType::STATIC_COPY, GL_STATIC_COPY>
	>;
};

template <>
struct Tools::Utils::MappingFor<Rendering::Settings::EUniformType, GLenum>
{
	using EnumType = Rendering::Settings::EUniformType;
	using type = std::tuple<
		EnumValuePair<EnumType::BOOL, GL_BOOL>,
		EnumValuePair<EnumType::INT, GL_INT>,
		EnumValuePair<EnumType::FLOAT, GL_FLOAT>,
		EnumValuePair<EnumType::FLOAT_VEC2, GL_FLOAT_VEC2>,
		EnumValuePair<EnumType::FLOAT_VEC3, GL_FLOAT_VEC3>,
		EnumValuePair<EnumType::FLOAT_VEC4, GL_FLOAT_VEC4>,
		EnumValuePair<EnumType::FLOAT_MAT3, GL_FLOAT_MAT3>,
		EnumValuePair<EnumType::FLOAT_MAT4, GL_FLOAT_MAT4>,
		EnumValuePair<EnumType::DOUBLE_MAT4, GL_DOUBLE_MAT4>,
		EnumValuePair<EnumType::SAMPLER_2D, GL_SAMPLER_2D>,
		EnumValuePair<EnumType::SAMPLER_2DARRAY, GL_SAMPLER_2D_ARRAY>,
		EnumValuePair<EnumType::SAMPLER_BUFFER, GL_SAMPLER_BUFFER>,
		EnumValuePair<EnumType::UINTSAMPLER_BUFFER, GL_UNSIGNED_INT_SAMPLER_BUFFER>,
		EnumValuePair<EnumType::INTSAMPLER_BUFFER, GL_INT_SAMPLER_BUFFER>,
		EnumValuePair<EnumType::SAMPLER_CUBE, GL_SAMPLER_CUBE>
	>;
};

template <>
struct Tools::Utils::MappingFor<Rendering::Settings::EInternalFormat, GLenum>
{
	using EnumType = Rendering::Settings::EInternalFormat;
	using type = std::tuple <
		EnumValuePair<EnumType::DEPTH_COMPONENT, GL_DEPTH_COMPONENT>,
		EnumValuePair<EnumType::DEPTH_COMPONENT32, GL_DEPTH_COMPONENT32>,
		EnumValuePair<EnumType::DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT32F>,
		EnumValuePair<EnumType::DEPTH_STENCIL, GL_DEPTH_STENCIL>,
		EnumValuePair<EnumType::DEPTH32F_STENCIL8, GL_DEPTH32F_STENCIL8>,
		EnumValuePair<EnumType::RED, GL_RED>,
		EnumValuePair<EnumType::RG, GL_RG>,
		EnumValuePair<EnumType::RGB, GL_RGB>,
		EnumValuePair<EnumType::RGBA, GL_RGBA>,
		EnumValuePair<EnumType::R8, GL_R8>,
		EnumValuePair<EnumType::R8_SNORM, GL_R8_SNORM>,
		EnumValuePair<EnumType::R16, GL_R16>,
		EnumValuePair<EnumType::R16_SNORM, GL_R16_SNORM>,
		EnumValuePair<EnumType::RG8, GL_RG8>,
		EnumValuePair<EnumType::RG8_SNORM, GL_RG8_SNORM>,
		EnumValuePair<EnumType::RG16, GL_RG16>,
		EnumValuePair<EnumType::RG16_SNORM, GL_RG16_SNORM>,
		EnumValuePair<EnumType::R3_G3_B2, GL_R3_G3_B2>,
		EnumValuePair<EnumType::RGB4, GL_RGB4>,
		EnumValuePair<EnumType::RGB5, GL_RGB5>,
		EnumValuePair<EnumType::RGB8, GL_RGB8>,
		EnumValuePair<EnumType::RGB8_SNORM, GL_RGB8_SNORM>,
		EnumValuePair<EnumType::RGB10, GL_RGB10>,
		EnumValuePair<EnumType::RGB12, GL_RGB12>,
		EnumValuePair<EnumType::RGB16_SNORM, GL_RGB16_SNORM>,
		EnumValuePair<EnumType::RGBA2, GL_RGBA2>,
		EnumValuePair<EnumType::RGBA4, GL_RGBA4>,
		EnumValuePair<EnumType::RGB5_A1, GL_RGB5_A1>,
		EnumValuePair<EnumType::RGBA8, GL_RGBA8>,
		EnumValuePair<EnumType::RGBA8_SNORM, GL_RGBA8_SNORM>,
		EnumValuePair<EnumType::RGB10_A2, GL_RGB10_A2>,
		EnumValuePair<EnumType::RGB10_A2UI, GL_RGB10_A2UI>,
		EnumValuePair<EnumType::RGBA12, GL_RGBA12>,
		EnumValuePair<EnumType::RGBA16, GL_RGBA16>,
		EnumValuePair<EnumType::SRGB8, GL_SRGB8>,
		EnumValuePair<EnumType::SRGB8_ALPHA8, GL_SRGB8_ALPHA8>,
		EnumValuePair<EnumType::R16F, GL_R16F>,
		EnumValuePair<EnumType::RG16F, GL_RG16F>,
		EnumValuePair<EnumType::RGB16F, GL_RGB16F>,
		EnumValuePair<EnumType::RGBA16F, GL_RGBA16F>,
		EnumValuePair<EnumType::R32F, GL_R32F>,
		EnumValuePair<EnumType::RG32F, GL_RG32F>,
		EnumValuePair<EnumType::RGB32F, GL_RGB32F>,
		EnumValuePair<EnumType::RGBA32F, GL_RGBA32F>,
		EnumValuePair<EnumType::R11F_G11F_B10F, GL_R11F_G11F_B10F>,
		EnumValuePair<EnumType::RGB9_E5, GL_RGB9_E5>,
		EnumValuePair<EnumType::R8I, GL_R8I>,
		EnumValuePair<EnumType::R8UI, GL_R8UI>,
		EnumValuePair<EnumType::R16I, GL_R16I>,
		EnumValuePair<EnumType::R16UI, GL_R16UI>,
		EnumValuePair<EnumType::R32I, GL_R32I>,
		EnumValuePair<EnumType::R32UI, GL_R32UI>,
		EnumValuePair<EnumType::RG8I, GL_RG8I>,
		EnumValuePair<EnumType::RG8UI, GL_RG8UI>,
		EnumValuePair<EnumType::RG16I, GL_RG16I>,
		EnumValuePair<EnumType::RG16UI, GL_RG16UI>,
		EnumValuePair<EnumType::RG32I, GL_RG32I>,
		EnumValuePair<EnumType::RG32UI, GL_RG32UI>,
		EnumValuePair<EnumType::RGB8I, GL_RGB8I>,
		EnumValuePair<EnumType::RGB8UI, GL_RGB8UI>,
		EnumValuePair<EnumType::RGB16I, GL_RGB16I>,
		EnumValuePair<EnumType::RGB16UI, GL_RGB16UI>,
		EnumValuePair<EnumType::RGB32I, GL_RGB32I>,
		EnumValuePair<EnumType::RGB32UI, GL_RGB32UI>,
		EnumValuePair<EnumType::RGBA8I, GL_RGBA8I>,
		EnumValuePair<EnumType::RGBA8UI, GL_RGBA8UI>,
		EnumValuePair<EnumType::RGBA16I, GL_RGBA16I>,
		EnumValuePair<EnumType::RGBA16UI, GL_RGBA16UI>,
		EnumValuePair<EnumType::RGBA32I, GL_RGBA32I>,
		EnumValuePair<EnumType::RGBA32UI, GL_RGBA32UI>,
		EnumValuePair<EnumType::COMPRESSED_RED, GL_COMPRESSED_RED>,
		EnumValuePair<EnumType::COMPRESSED_RG, GL_COMPRESSED_RG>,
		EnumValuePair<EnumType::COMPRESSED_RGB, GL_COMPRESSED_RGB>,
		EnumValuePair<EnumType::COMPRESSED_RGBA, GL_COMPRESSED_RGBA>,
		EnumValuePair<EnumType::COMPRESSED_SRGB, GL_COMPRESSED_SRGB>,
		EnumValuePair<EnumType::COMPRESSED_SRGB_ALPHA, GL_COMPRESSED_SRGB_ALPHA>,
		EnumValuePair<EnumType::COMPRESSED_RED_RGTC1, GL_COMPRESSED_RED_RGTC1>,
		EnumValuePair<EnumType::COMPRESSED_SIGNED_RED_RGTC1, GL_COMPRESSED_SIGNED_RED_RGTC1>,
		EnumValuePair<EnumType::COMPRESSED_RG_RGTC2, GL_COMPRESSED_RG_RGTC2>,
		EnumValuePair<EnumType::COMPRESSED_SIGNED_RG_RGTC2, GL_COMPRESSED_SIGNED_RG_RGTC2>,
		EnumValuePair<EnumType::COMPRESSED_RGBA_BPTC_UNORM, GL_COMPRESSED_RGBA_BPTC_UNORM>,
		EnumValuePair<EnumType::COMPRESSED_SRGB_ALPHA_BPTC_UNORM, GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM>,
		EnumValuePair<EnumType::COMPRESSED_RGB_BPTC_SIGNED_FLOAT, GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT>,
		EnumValuePair<EnumType::COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT, GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT>
	> ;
};

template <>
struct Tools::Utils::MappingFor<Rendering::Settings::EShaderType, GLenum>
{
	using EnumType = Rendering::Settings::EShaderType;
	using type = std::tuple<
		EnumValuePair<EnumType::VERTEX, GL_VERTEX_SHADER>,
		EnumValuePair<EnumType::FRAGMENT, GL_FRAGMENT_SHADER>,
		EnumValuePair<EnumType::GEOMETRY, GL_GEOMETRY_SHADER>,
		EnumValuePair<EnumType::COMPUTE, GL_COMPUTE_SHADER>,
		EnumValuePair<EnumType::TESSELLATION_CONTROL, GL_TESS_CONTROL_SHADER>,
		EnumValuePair<EnumType::TESSELLATION_EVALUATION, GL_TESS_EVALUATION_SHADER>
	>;
};

template <>
struct Tools::Utils::MappingFor<Rendering::Settings::EFramebufferAttachment, GLenum>
{
	using EnumType = Rendering::Settings::EFramebufferAttachment;
	using type = std::tuple<
		EnumValuePair<EnumType::COLOR, GL_COLOR_ATTACHMENT0>,
		EnumValuePair<EnumType::DEPTH, GL_DEPTH_ATTACHMENT>,
		EnumValuePair<EnumType::STENCIL, GL_STENCIL_ATTACHMENT>,
		EnumValuePair<EnumType::DEPTH_STENCIL, GL_DEPTH_STENCIL_ATTACHMENT>
	>;
};

template <>
struct Tools::Utils::MappingFor<Rendering::Settings::EBufferType, GLenum>
{
	using EnumType = Rendering::Settings::EBufferType;
	using type = std::tuple<
		EnumValuePair<EnumType::VERTEX, GL_ARRAY_BUFFER>,
		EnumValuePair<EnumType::INDEX, GL_ELEMENT_ARRAY_BUFFER>,
		EnumValuePair<EnumType::UNIFORM, GL_UNIFORM_BUFFER>,
		EnumValuePair<EnumType::SHADER_STORAGE, GL_SHADER_STORAGE_BUFFER>,
		EnumValuePair<EnumType::PIXEL_PACK, GL_PIXEL_PACK_BUFFER>
	>;
};

template <>
struct Tools::Utils::MappingFor<Rendering::Settings::ETextureType, GLenum>
{
	using EnumType = Rendering::Settings::ETextureType;
	using type = std::tuple<
		EnumValuePair<EnumType::TEXTURE_2D, GL_TEXTURE_2D>,
		EnumValuePair<EnumType::TEXTURE_2DARRAY, GL_TEXTURE_2D_ARRAY>,
		EnumValuePair<EnumType::TEXTURE_BUFFER, GL_TEXTURE_BUFFER>,
		EnumValuePair<EnumType::TEXTURE_2DMULSAMPLE, GL_TEXTURE_2D_MULTISAMPLE>,
		EnumValuePair<EnumType::TEXTURE_CUBE, GL_TEXTURE_CUBE_MAP>
	>;
};
