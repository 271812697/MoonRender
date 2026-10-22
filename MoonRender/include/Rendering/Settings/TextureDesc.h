#pragma once
#include <Rendering/Settings/ETextureFilteringMode.h>
#include <Rendering/Settings/ETextureWrapMode.h>
#include <Rendering/Settings/EInternalFormat.h>
#include <Rendering/Settings/EFormat.h>
#include <Rendering/Settings/EPixelDataType.h>

namespace Rendering::Settings
{
	/**
	* Structure that holds additional data for mutable textures
	*/
	struct MutableTextureDesc
	{
		EFormat format = EFormat::RGBA;
		EPixelDataType type = EPixelDataType::UNSIGNED_BYTE;
		const void* data = nullptr;
		int arrayLayers = 0;//use this in case of GL_TEXTURE_2D_ARRAY
	};

	/**
	* Structure that holds the description of a texture
	*/
	struct TextureDesc
	{
		uint32_t width = 0;
		uint32_t height = 0;
		ETextureFilteringMode minFilter = ETextureFilteringMode::LINEAR_MIPMAP_LINEAR;
		ETextureFilteringMode magFilter = ETextureFilteringMode::LINEAR;
		ETextureWrapMode horizontalWrap = ETextureWrapMode::REPEAT;
		ETextureWrapMode verticalWrap = ETextureWrapMode::REPEAT;
		EInternalFormat internalFormat = EInternalFormat::RGBA;
		bool useMipMaps = true;
		/** Explicit mip count; 0 lets the backend use the full chain. Filament's IBL
		 * prefilter uses 5 roughness levels, not the full mip chain. */
		uint32_t mipLevels = 0;
		bool isTextureBuffer = false;
		unsigned int buffetLen = 0;
		unsigned int texBufferId=0;
		std::optional<MutableTextureDesc> mutableDesc = std::nullopt;
	};
}
