#pragma once
#include <optional>
#include <string>
#include <vector>

#include <Maths/FVector4.h>
#include <Rendering/Backend/BackendResources.h>
#include <Rendering/HAL/Common/BackendConstructionTag.h>
#include <Rendering/Settings/EGraphicsBackend.h>
#include <Rendering/Settings/ETextureType.h>
#include <Rendering/Settings/TextureDesc.h>

namespace Rendering::HAL
{
	/**
	* Represents a texture, used to store image data for the graphics backend to use.
	*
	* This is the *backend* object, not the front end: it is constructed in the slot a handle
	* names (see Backend::Records::CreateTexture), it owns the GL name, and its methods are the GL
	* operations the executor runs (Filament's GLTextureObject, and the methods of its driver).
	* What the recording side holds instead is a handle, wrapped by Rendering::Data::Texture, which
	* only records commands - the same split as HAL::Buffer and Data::BufferObject.
	*
	* It derives from Backend::HwTexture, which is where the kind of texture, the description its
	* storage was allocated with and the debug name live: what anything holding just the handle can
	* read (a render target sizes itself from its attachments, the material descriptors report a
	* sampler's size). What is left here is what only the side that owns the context can touch.
	*/
	template<Settings::EGraphicsBackend Backend, class TextureContext>
	class TTexture : public Rendering::Backend::HwTexture
	{
	public:
		/**
		* Creates the backend object: the GL name is made here, so this runs where the context is
		* (the executor of a create-texture command). The recording side never runs it.
		* @param p_type The type of the texture (2D, Cube, etc.)
		* @param p_debugName A name used to identify the texture for debugging purposes
		*/
		TTexture(Settings::ETextureType p_type, std::string_view p_debugName, BackendConstructionTag);

		/**
		* Destroys the texture. The GL name (and a texture buffer's data buffer) goes here: this
		* object only ever exists on the side that owns the context, and Backend::Records::
		* DestroyTexture queues the deallocation to that side.
		*/
		~TTexture();

		TTexture(const TTexture&) = delete;
		TTexture& operator=(const TTexture&) = delete;

		/**
		* Allocates storage for the texture.
		* @param p_desc
		*/
		void Allocate(const Settings::TextureDesc& p_desc);

		/**
		* Returns true if the texture has been properly allocated.
		*/
		bool IsValid() const;

		/**
		* Returns true if the texture is mutable.
		*/
		bool IsMutable() const;

		/**
		* Uploads data to the texture.
		* @param p_data Pointer to the data to upload.
		* @param p_format Format of the data.
		* @param p_type Type of the pixel data.
		*/
		void Upload(const void* p_data, Settings::EFormat p_format, Settings::EPixelDataType p_type);

		/** Uploads a sub-rectangle into storage that already exists (Filament's update2DImage on a
		 * mutable texture). */
		void UploadSubImage(
			const void* p_data,
			Settings::EFormat p_format,
			Settings::EPixelDataType p_type,
			uint32_t p_x,
			uint32_t p_y,
			uint32_t p_width,
			uint32_t p_height);

		/**
		* Resizes the texture.
		* @param p_width
		* @param p_height
		*/
		void Resize(uint32_t p_width, uint32_t p_height);

		/**
		* Returns the texture descriptor structure.
		*/
		const Settings::TextureDesc& GetDesc() const;

		/**
		* Generates mipmaps for the texture.
		*/
		void GenerateMipmaps() const;

		/**
		* Sets the border color for the texture.
		* @param p_color
		*/
		void SetBorderColor(const Maths::FVector4& p_color);

		int GetWidth();
		int GetHeight();

		/**
		* Returns the debug name of the texture.
		*/
		const std::string& GetDebugName() const;

		/** Binds the texture to the given texture unit (glBindTextureUnit). */
		void Bind(uint32_t p_slot) const;

		/** Binds the texture to its own target, for the paths that need the classic binding
		 * (glTexImage2D has no DSA form). */
		void Bind() const;

		/**
		* Unbinds the texture.
		*/
		void Unbind() const;

		/**
		* Returns the GL name of the texture.
		*/
		uint32_t GetID() const;

	private:
		TextureContext m_textureContext;
	};
}
