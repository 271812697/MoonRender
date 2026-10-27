#pragma once
#include <string>
#include <vector>
#include "Rendering/Resources/Texture.h"


namespace Rendering::Resources::Loaders
{

	class TextureLoader
	{
	public:
		TextureLoader() = delete;


		static Texture* Create(
			const std::string& p_filepath,
			Rendering::Settings::ETextureFilteringMode p_minFilter,
			Rendering::Settings::ETextureFilteringMode p_magFilter,
			Rendering::Settings::ETextureWrapMode p_horizontalWrapMode,
			Rendering::Settings::ETextureWrapMode p_verticalWrapMode,
			bool p_generateMipmap
		);

		static Texture* CreatePixel(uint8_t r, uint8_t g, uint8_t b, uint8_t a);

		/** A texture whose pixels can be replaced later (mutable storage), for images that are
		 * generated at runtime - the view cube's face labels, for one. CreateFromMemory allocates
		 * immutable storage, so a later Upload would only touch the CPU copy. */
		static Texture* CreateMutable(
			uint32_t p_width,
			uint32_t p_height,
			const void* p_rgba8,
			Rendering::Settings::ETextureFilteringMode p_minFilter,
			Rendering::Settings::ETextureFilteringMode p_magFilter,
			Rendering::Settings::ETextureWrapMode p_horizontalWrapMode,
			Rendering::Settings::ETextureWrapMode p_verticalWrapMode,
			bool p_generateMipmap
		);


		static Texture* CreateFromMemory(
			uint8_t* p_data,
			uint32_t p_width,
			uint32_t p_height,
			Rendering::Settings::ETextureFilteringMode p_minFilter,
			Rendering::Settings::ETextureFilteringMode p_magFilter,
			Rendering::Settings::ETextureWrapMode p_horizontalWrapMode,
			Rendering::Settings::ETextureWrapMode p_verticalWrapMode,
			bool p_generateMipmap
		);

		static void Reload(
			Texture& p_texture,
			const std::string& p_filePath,
			Rendering::Settings::ETextureFilteringMode p_minFilter,
			Rendering::Settings::ETextureFilteringMode p_magFilter,
			Rendering::Settings::ETextureWrapMode p_horizontalWrapMode,
			Rendering::Settings::ETextureWrapMode p_verticalWrapMode,
			bool p_generateMipmap
		);

		static bool Destroy(Texture*& p_textureInstance);
	};
}