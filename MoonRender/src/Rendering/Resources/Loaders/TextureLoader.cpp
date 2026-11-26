#include <array>
#include <memory>
#include <vector>

#include <stb_image/stb_image.h>

#include <Core/Context.h>
#include <Core/RenderModuleHost.h>
#include <Rendering/Backend/BackendResources.h>
#include <Rendering/Context/Driver.h>
#include <Rendering/Resources/Loaders/TextureLoader.h>
#include <Tools/Utils/PathParser.h>

namespace
{
	/**
	* Simple wrapper for stb_image. Handles SDR and HDR image loading,
	* and enforces RAII for the loaded data.
	*/
	struct Image
	{
		int width = 0;
		int height = 0;
		int bpp = 0;
		bool isHDR = false;
		void* data = nullptr;

		Image(const std::string& p_filepath)
		{
			stbi_set_flip_vertically_on_load(true);

			isHDR = stbi_is_hdr(p_filepath.c_str());

			data = isHDR ?
				static_cast<void*>(stbi_loadf(p_filepath.c_str(), &width, &height, &bpp, 4)) :
				static_cast<void*>(stbi_load(p_filepath.c_str(), &width, &height, &bpp, 4));
		}

		virtual ~Image() { stbi_image_free(data); }
		bool IsValid() const { return data; }
		operator bool() const { return IsValid(); }
	};

	void PrepareTexture(
		Rendering::Data::Texture& p_texture,
		void* p_data,
		Rendering::Settings::ETextureFilteringMode p_minFilter,
		Rendering::Settings::ETextureFilteringMode p_magFilter,
		Rendering::Settings::ETextureWrapMode p_horizontalWrapMode,
		Rendering::Settings::ETextureWrapMode p_verticalWrapMode,
		uint32_t p_width,
		uint32_t p_height,
		bool p_generateMipmap,
		bool p_hdr
	)
	{
		using namespace Rendering::Settings;

		p_texture.Allocate({
			.width = p_width,
			.height = p_height,
			.minFilter = p_minFilter,
			.magFilter = p_magFilter,
			.horizontalWrap = p_horizontalWrapMode,
			.verticalWrap = p_verticalWrapMode,
			.internalFormat = p_hdr ? EInternalFormat::RGBA32F : EInternalFormat::RGBA8,
			.useMipMaps = p_generateMipmap
			});

		p_texture.Upload(p_data, EFormat::RGBA, p_hdr ? EPixelDataType::FLOAT : EPixelDataType::UNSIGNED_BYTE);

		if (p_generateMipmap)
		{
			p_texture.GenerateMipmaps();
		}
	}

}

Rendering::Resources::Texture* Rendering::Resources::Loaders::TextureLoader::Create(
	const std::string& p_filepath,
	Rendering::Settings::ETextureFilteringMode p_minFilter,
	Rendering::Settings::ETextureFilteringMode p_magFilter,
	Rendering::Settings::ETextureWrapMode p_horizontalWrapMode,
	Rendering::Settings::ETextureWrapMode p_verticalWrapMode,
	bool p_generateMipmap
)
{
	if (Image image{ p_filepath })
	{
		Texture* result = new Texture{ p_filepath,
			Tools::Utils::PathParser::GetElementName(p_filepath) };
		PrepareTexture(result->GetTexture(), image.data, p_minFilter, p_magFilter,
			p_horizontalWrapMode, p_verticalWrapMode,
			static_cast<uint32_t>(image.width), static_cast<uint32_t>(image.height),
			p_generateMipmap, image.isHDR);
		return result;
	}

	return nullptr;
}

Rendering::Resources::Texture* Rendering::Resources::Loaders::TextureLoader::CreatePixel(
	uint8_t r,
	uint8_t g,
	uint8_t b,
	uint8_t a
)
{
	std::array<uint8_t, 4> colorData = { r, g, b, a };

	return Rendering::Resources::Loaders::TextureLoader::CreateFromMemory(
		colorData.data(), 1, 1,
		Rendering::Settings::ETextureFilteringMode::NEAREST,
		Rendering::Settings::ETextureFilteringMode::NEAREST,
		Rendering::Settings::ETextureWrapMode::REPEAT,
		Rendering::Settings::ETextureWrapMode::REPEAT,
		false
	);
}

Rendering::Resources::Texture* Rendering::Resources::Loaders::TextureLoader::CreateFromMemory(
	uint8_t* p_data,
	uint32_t p_width,
	uint32_t p_height,
	Rendering::Settings::ETextureFilteringMode p_minFilter,
	Rendering::Settings::ETextureFilteringMode p_magFilter,
	Rendering::Settings::ETextureWrapMode p_horizontalWrapMode,
	Rendering::Settings::ETextureWrapMode p_verticalWrapMode,
	bool p_generateMipmap
)
{
	Texture* result = new Texture("", "FromMemory");
	PrepareTexture(result->GetTexture(), p_data, p_minFilter, p_magFilter,
		p_horizontalWrapMode, p_verticalWrapMode, p_width, p_height, p_generateMipmap, false);
	return result;
}

Rendering::Resources::Texture* Rendering::Resources::Loaders::TextureLoader::CreateMutable(
	uint32_t p_width,
	uint32_t p_height,
	const void* p_rgba8,
	Rendering::Settings::ETextureFilteringMode p_minFilter,
	Rendering::Settings::ETextureFilteringMode p_magFilter,
	Rendering::Settings::ETextureWrapMode p_horizontalWrapMode,
	Rendering::Settings::ETextureWrapMode p_verticalWrapMode,
	bool p_generateMipmap
)
{
	using namespace Rendering::Settings;

	// A mutable texture: the storage is described by this allocation, and a later upload into it
	// does not re-specify it (the font atlas and the view cube's labels replace their contents).
	Texture* result = new Texture("", "RuntimeGenerated");
	Rendering::Data::Texture& texture = result->GetTexture();
	texture.Allocate({
		.width = p_width,
		.height = p_height,
		.minFilter = p_minFilter,
		.magFilter = p_magFilter,
		.horizontalWrap = p_horizontalWrapMode,
		.verticalWrap = p_verticalWrapMode,
		.internalFormat = EInternalFormat::RGBA8,
		.useMipMaps = p_generateMipmap,
		.mutableDesc = MutableTextureDesc{
			.format = EFormat::RGBA,
			.type = EPixelDataType::UNSIGNED_BYTE,
			.data = nullptr
		}
	});
	texture.Upload(p_rgba8, EFormat::RGBA, EPixelDataType::UNSIGNED_BYTE);
	if (p_generateMipmap)
	{
		texture.GenerateMipmaps();
	}

	return result;
}

void Rendering::Resources::Loaders::TextureLoader::Reload(
	Texture& p_texture,
	const std::string& p_filePath,
	Rendering::Settings::ETextureFilteringMode p_minFilter,
	Rendering::Settings::ETextureFilteringMode p_magFilter,
	Rendering::Settings::ETextureWrapMode p_horizontalWrapMode,
	Rendering::Settings::ETextureWrapMode p_verticalWrapMode,
	bool p_generateMipmap
)
{
	if (Image image{ p_filePath })
	{
		// Reload replaces the contents of a texture that already exists.
		PrepareTexture(p_texture.GetTexture(), image.data, p_minFilter, p_magFilter,
			p_horizontalWrapMode, p_verticalWrapMode,
			static_cast<uint32_t>(image.width), static_cast<uint32_t>(image.height),
			p_generateMipmap, image.isHDR);
	}
}

bool Rendering::Resources::Loaders::TextureLoader::Destroy(Texture*& p_textureInstance)
{
	if (p_textureInstance)
	{
		delete p_textureInstance;
		p_textureInstance = nullptr;
		return true;
	}

	return false;
}
