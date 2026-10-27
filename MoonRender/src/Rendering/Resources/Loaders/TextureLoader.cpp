


#include <array>
#include <format>
#include <memory>
#include <stb_image/stb_image.h>
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
		Rendering::HAL::Texture& p_texture,
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
		auto texture = std::make_unique<HAL::Texture>(
			Settings::ETextureType::TEXTURE_2D,
			Tools::Utils::PathParser::GetElementName(p_filepath)
		);

		PrepareTexture(
			*texture,
			image.data,
			p_minFilter,
			p_magFilter,
			p_horizontalWrapMode,
			p_verticalWrapMode,
			image.width,
			image.height,
			p_generateMipmap,
			image.isHDR
		);

		return new Texture{ p_filepath, std::move(texture) };
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
	auto texture = std::make_unique<HAL::Texture>(Settings::ETextureType::TEXTURE_2D, "FromMemory");

	PrepareTexture(
		*texture,
		p_data,
		p_minFilter,
		p_magFilter,
		p_horizontalWrapMode,
		p_verticalWrapMode,
		p_width,
		p_height,
		p_generateMipmap,
		false
	);

	return new Texture("", std::move(texture));
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

	auto texture = std::make_unique<HAL::Texture>(ETextureType::TEXTURE_2D, "RuntimeGenerated");
	texture->Allocate({
		.width = p_width,
		.height = p_height,
		.minFilter = p_minFilter,
		.magFilter = p_magFilter,
		.horizontalWrap = p_horizontalWrapMode,
		.verticalWrap = p_verticalWrapMode,
		.internalFormat = EInternalFormat::RGBA8,
		.useMipMaps = p_generateMipmap,
		// The mutable descriptor is what lets Upload() reach the GPU copy later.
		.mutableDesc = MutableTextureDesc{
			.format = EFormat::RGBA,
			.type = EPixelDataType::UNSIGNED_BYTE,
			.data = p_rgba8
		}
		});

	if (p_generateMipmap)
	{
		texture->GenerateMipmaps();
	}

	return new Texture("", std::move(texture));
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
		auto texture = std::make_unique<HAL::Texture>(
			Settings::ETextureType::TEXTURE_2D,
		Tools::Utils::PathParser::GetElementName(p_filePath)
		);

		PrepareTexture(
			*texture,
			image.data,
			p_minFilter,
			p_magFilter,
			p_horizontalWrapMode,
			p_verticalWrapMode,
			image.width,
			image.height,
			p_generateMipmap,
			image.isHDR
		);

		p_texture.SetTexture(std::move(texture));
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
