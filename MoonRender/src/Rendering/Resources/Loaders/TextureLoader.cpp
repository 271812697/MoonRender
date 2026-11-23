#include <array>
#include <memory>
#include <vector>

#include <stb_image/stb_image.h>

#include <Core/Context.h>
#include <Core/RenderModuleHost.h>
#include <Rendering/Backend/BackendResources.h>
#include <Rendering/Backend/CreateTextureRequest.h>
#include <Rendering/Backend/TextureUploadRequest.h>
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

	/** The driver, when there is a render module to record into. */
	Rendering::Context::Driver* GetDriver()
	{
		auto* module = ::Core::RenderModuleHost::Instance();
		return module != nullptr && module->GetContext() != nullptr
			? module->GetContext()->driver.get() : nullptr;
	}

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

	/** Records the creation as a driver command (Filament's createTexture).
	 *
	 * The object is made on the side that owns the context; anything recorded after this - the
	 * upload, the draws sampling it - runs later in the ring, so it finds the texture. Without a
	 * driver the object is created here, which is what the code did before there was a command.
	 */
	void RecordCreate(Rendering::Resources::Texture& p_texture, std::string p_name)
	{
		Rendering::Context::Driver* const driver = GetDriver();
		if (driver == nullptr)
		{
			Rendering::Backend::Records::CreateTexture(p_texture.GetHandle(),
				Rendering::Settings::ETextureType::TEXTURE_2D, p_name);
			return;
		}

		Rendering::Backend::CreateTextureRequest request;
		request.handleId = p_texture.GetHandle().GetId();
		request.type = Rendering::Settings::ETextureType::TEXTURE_2D;
		request.debugName = std::move(p_name);
		driver->CreateTexture(std::move(request));
	}

	/** Records the allocate+upload as a driver command (Filament's update2DImage): the command
	 * carries its own copy of the pixels, so the caller's buffer can go away immediately. */
	void RecordPrepare(
		Rendering::Resources::Texture& p_texture,
		const void* p_data,
		size_t p_size,
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
		Rendering::Context::Driver* const driver = GetDriver();
		if (driver == nullptr || !p_texture.GetHandle())
		{
			PrepareTexture(p_texture.GetTexture(), const_cast<void*>(p_data), p_minFilter, p_magFilter,
				p_horizontalWrapMode, p_verticalWrapMode, p_width, p_height, p_generateMipmap, p_hdr);
			return;
		}

		Rendering::Backend::TextureUploadRequest request;
		request.handleId = p_texture.GetHandle().GetId();
		request.descriptor = {
			.width = p_width,
			.height = p_height,
			.minFilter = p_minFilter,
			.magFilter = p_magFilter,
			.horizontalWrap = p_horizontalWrapMode,
			.verticalWrap = p_verticalWrapMode,
			.internalFormat = p_hdr ? Rendering::Settings::EInternalFormat::RGBA32F
				: Rendering::Settings::EInternalFormat::RGBA8,
			.useMipMaps = p_generateMipmap
		};
		request.format = Rendering::Settings::EFormat::RGBA;
		request.type = p_hdr ? Rendering::Settings::EPixelDataType::FLOAT
			: Rendering::Settings::EPixelDataType::UNSIGNED_BYTE;
		request.pixels.assign(static_cast<const uint8_t*>(p_data),
			static_cast<const uint8_t*>(p_data) + p_size);
		request.generateMipmaps = p_generateMipmap;
		driver->UploadTexture(std::move(request));
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
		Texture* result = new Texture{ p_filepath };
		RecordCreate(*result, Tools::Utils::PathParser::GetElementName(p_filepath));
		RecordPrepare(*result, image.data,
			static_cast<size_t>(image.width) * image.height * 4 * (image.isHDR ? sizeof(float) : 1),
			p_minFilter, p_magFilter, p_horizontalWrapMode, p_verticalWrapMode,
			image.width, image.height, p_generateMipmap, image.isHDR);
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
	Texture* result = new Texture("");
	RecordCreate(*result, "FromMemory");
	RecordPrepare(*result, p_data,
		static_cast<size_t>(p_width) * p_height * 4,
		p_minFilter, p_magFilter, p_horizontalWrapMode, p_verticalWrapMode,
		p_width, p_height, p_generateMipmap, false);
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

	Texture* result = new Texture("");
	Rendering::Backend::CreateTextureRequest request;
	request.handleId = result->GetHandle().GetId();
	request.type = ETextureType::TEXTURE_2D;
	request.debugName = "RuntimeGenerated";
	request.allocate = true;
	request.descriptor = {
		.width = p_width,
		.height = p_height,
		.minFilter = p_minFilter,
		.magFilter = p_magFilter,
		.horizontalWrap = p_horizontalWrapMode,
		.verticalWrap = p_verticalWrapMode,
		.internalFormat = EInternalFormat::RGBA8,
		.useMipMaps = p_generateMipmap,
		// The mutable descriptor is what lets a later upload reach the GPU copy.
		.mutableDesc = MutableTextureDesc{
			.format = EFormat::RGBA,
			.type = EPixelDataType::UNSIGNED_BYTE,
			.data = nullptr
		}
	};
	request.pixels.assign(static_cast<const uint8_t*>(p_rgba8),
		static_cast<const uint8_t*>(p_rgba8) + static_cast<size_t>(p_width) * p_height * 4);
	request.generateMipmaps = p_generateMipmap;

	// Recorded, then drained: the object has to exist for the caller that binds it next, and the
	// request carries the pixels so nothing here touches GL.
	if (Rendering::Context::Driver* const driver = GetDriver())
	{
		driver->CreateTexture(std::move(request));
	}
	else
	{
		Rendering::HAL::Texture* const texture = Rendering::Backend::Records::CreateTexture(
			result->GetHandle(), ETextureType::TEXTURE_2D, "RuntimeGenerated");
		if (texture != nullptr)
		{
			auto descriptor = request.descriptor;
			descriptor.mutableDesc.value().data = request.pixels.data();
			texture->Allocate(descriptor);
			if (p_generateMipmap)
			{
				texture->GenerateMipmaps();
			}
		}
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
		RecordPrepare(p_texture, image.data,
			static_cast<size_t>(image.width) * image.height * 4 * (image.isHDR ? sizeof(float) : 1),
			p_minFilter, p_magFilter, p_horizontalWrapMode, p_verticalWrapMode,
			image.width, image.height, p_generateMipmap, image.isHDR);
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
