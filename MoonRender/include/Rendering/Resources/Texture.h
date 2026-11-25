#pragma once
#include <cstdint>
#include <string>
#include <string_view>

#include <Rendering/Backend/ResourceHandle.h>
#include <Rendering/Data/Texture.h>

namespace Rendering::Resources
{
	namespace Loaders { class TextureLoader; }

	/**
	* A texture asset: the path the editor and the .mat files name, plus the GPU half that path
	* stands for.
	*
	* The GPU half is Rendering::Data::Texture - the front end, which owns the handle and records
	* every operation - and its backend object is HAL::Texture, created on the side that owns the
	* context by the CreateTexture command. That is the same three-way split the buffers have
	* (Resources::Texture / Data::Texture / HAL::Buffer), and Filament's Texture/HwTexture split
	* underneath it.
	*/
	class Texture
	{
		friend class Loaders::TextureLoader;

	public:
		Texture();
		explicit Texture(const std::string p_path);
		Texture(const std::string p_path, std::string_view p_debugName);
		/** A texture that is not a 2D image asset: the overlay's texture buffers are built this
		 * way, because their kind is fixed at creation and nothing else names it. */
		explicit Texture(Settings::ETextureType p_type, std::string_view p_debugName = std::string_view{});
		~Texture();

		Texture(Texture const&) = delete;
		Texture& operator=(Texture const&) = delete;

		/** The front-end texture: what allocates, uploads and names the backend object. */
		Data::Texture& GetTexture() { return m_texture; }
		const Data::Texture& GetTexture() const { return m_texture; }

		/** The handle of the backend object (what queued work and material descriptors name). */
		Rendering::Backend::ResourceHandle<Rendering::Backend::HwTexture> GetHandle() const
		{
			return m_texture.GetHandle();
		}

		const std::string path;

	private:
		Data::Texture m_texture;
	};
}
