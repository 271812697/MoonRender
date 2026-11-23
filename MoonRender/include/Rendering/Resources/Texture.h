#pragma once
#include <cstdint>
#include <string>

#include <Rendering/Backend/ResourceHandle.h>
#include <Rendering/HAL/Texture.h>
#include <Rendering/Settings/ETextureFilteringMode.h>

namespace Rendering::Resources
{
	namespace Loaders { class TextureLoader; }

	/**
	* A texture. Front end only: the handle names it, the backend object lives in the slot the
	* handle names (Backend::HandleAllocator, like Filament's HwTexture) and is created on the side
	* that owns the context - by the CreateTexture command, or by the loader where the object is
	* needed at once (the mutables the overlay uploads into later). That is Filament's
	* Texture/HwTexture split.
	*/
	class Texture
	{
		friend class Loaders::TextureLoader;

	public:
		/** Default-constructed textures (the runtime ones: Im3D's texture buffers, the overlay's) get a
		 * handle too - it is what their material descriptors and queued work name. */
		Texture();
		explicit Texture(const std::string p_path);
		~Texture();

		Texture(Texture const&) = delete;
		Texture& operator=(Texture const&) = delete;

		/** The backend object. Asserts when it has not been created yet - which only happens if a
		 * caller asks for it before the CreateTexture command has run. */
		HAL::Texture& GetTexture();

		/** The front-end half: queued work names this, not the pointer. */
		Rendering::Backend::ResourceHandle<Rendering::HAL::Texture> GetHandle() const { return m_handle; }

		const std::string path;

	private:
		Rendering::Backend::ResourceHandle<Rendering::HAL::Texture> m_handle;
	};
}
