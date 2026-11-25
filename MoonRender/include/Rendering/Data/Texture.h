#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <Maths/FVector4.h>
#include <Rendering/Backend/ResourceHandle.h>
#include <Rendering/Settings/EFormat.h>
#include <Rendering/Settings/EPixelDataType.h>
#include <Rendering/Settings/ETextureType.h>
#include <Rendering/Settings/TextureDesc.h>

namespace Rendering::Backend
{
	struct HwTexture;
}

namespace Rendering::Data
{
	/** The front end of a texture: what the recording side owns (a material's maps, a pass's render
	 * targets, the overlay's atlases and texture buffers).
	 *
	 * It holds the handle that names the backend object - HAL::Texture, which lives in the handle's
	 * slot on the side that owns the context and is the only thing that has a GL name - plus the
	 * description its storage was allocated with and the allocated flag. The recording side reads
	 * those back (a target sizes itself from its attachments, a material descriptor reports a
	 * sampler's size) long before the executor has run anything, so they are mirrored here.
	 *
	 * Every method records a command, or, with no driver at all, does the work inline; none of them
	 * touches GL on the recording side. Filament's Texture, and the counterpart of
	 * Data::BufferObject - where Data::BufferObject wraps a buffer's handle, this wraps a
	 * texture's.
	 *
	 * The object owns its handle: copying is disabled, moving transfers it.
	 */
	class Texture
	{
	public:
		/** An empty object: no handle, no type. IsValid() is false; every operation is a no-op. */
		Texture() = default;

		/** Reserves the handle and records the creation of its backend object (Filament's
		 * createTexture: the type is fixed here, and the object is made where the context is). */
		explicit Texture(Settings::ETextureType p_type, std::string_view p_debugName = std::string_view{});

		/** Frees the record now and queues the GL deletion to the side that owns the context, so
		 * this is safe from any thread. */
		~Texture();

		Texture(const Texture&) = delete;
		Texture& operator=(const Texture&) = delete;
		Texture(Texture&& p_other) noexcept;
		Texture& operator=(Texture&& p_other) noexcept;

		/** Gives the texture its storage (glTextureStorage2D), discarding what it held. */
		void Allocate(const Settings::TextureDesc& p_desc);

		/** Whether the storage has been allocated (the flag the recording side set, so it answers
		 * before the allocate command has run). */
		bool IsValid() const { return m_allocated; }

		/** Whether a later upload may re-specify the storage (Filament's mutable texture). */
		bool IsMutable() const { return m_desc.mutableDesc.has_value(); }

		/** Uploads p_data into the texture, allocating the storage first when it has none. */
		void Upload(const void* p_data, Settings::EFormat p_format, Settings::EPixelDataType p_type);

		/** Uploads a sub-rectangle into storage that already exists. */
		void UploadSubImage(
			const void* p_data,
			Settings::EFormat p_format,
			Settings::EPixelDataType p_type,
			uint32_t p_x,
			uint32_t p_y,
			uint32_t p_width,
			uint32_t p_height);

		/** Re-specifies a mutable texture's storage. */
		void Resize(uint32_t p_width, uint32_t p_height);

		void GenerateMipmaps() const;
		void SetBorderColor(const Maths::FVector4& p_color);

		const Settings::TextureDesc& GetDesc() const;
		int GetWidth() const { return static_cast<int>(m_desc.width); }
		int GetHeight() const { return static_cast<int>(m_desc.height); }

		Settings::ETextureType GetType() const { return m_type; }
		const std::string& GetDebugName() const { return m_debugName; }

		/** The GL name, resolved on this side; 0 until the create command has run. */
		uint32_t GetID() const;

		/** Whether this object names a backend object. */
		bool HasHandle() const { return static_cast<bool>(m_handle); }

		const Rendering::Backend::ResourceHandle<Rendering::Backend::HwTexture>& GetHandle() const
		{
			return m_handle;
		}

		/** The pixels of the last upload, for the callers that read a texture's content on the CPU
		 * (the path tracer's BVH bakes its maps out of these). Filled by Upload. */
		std::vector<uint8_t> texData;

	private:
		Rendering::Backend::ResourceHandle<Rendering::Backend::HwTexture> m_handle;
		Settings::ETextureType m_type = Settings::ETextureType::TEXTURE_2D;
		Settings::TextureDesc m_desc;
		bool m_allocated = false;
		std::string m_debugName;
	};
}
