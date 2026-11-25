#pragma once
#include <cstdint>
#include <variant>

#include <Rendering/Backend/ResourceHandle.h>

namespace Rendering::Resources
{
	class Texture;
}

namespace Rendering::Backend
{
	struct HwBufferObject;
	struct HwTexture;
}

namespace Rendering::Data
{
	class Texture;
}

/** One slot of a DescriptorSet: the GPU resource a material binds at a binding point.
 *
 * A descriptor is either a buffer range (uniform buffer or shader-storage buffer: GL buffer
 * id + offset/length + binding point) or a texture + sampler pair. This mirrors filament's
 * DescriptorSet::Desc: the descriptor describes the GPU resource; the CPU uniform data lives
 * in the material instance, not here. */
namespace Rendering::Data
{
	class Descriptor
	{
	public:
		enum class Type : uint8_t
		{
			Invalid = 0,
			Buffer = 1,
			Texture = 2,
		};

		enum class BufferType : uint8_t
		{
			Uniform = 0,
			ShaderStorage = 1,
		};

		/** A buffer range, split the way the texture below is: the front end knows the *handle*,
		 * the executor knows the GL name it stands for (Filament's DescriptorSet::buffer). Storing
		 * a GL name here would pin the descriptor to a name that does not exist yet at record
		 * time, because creation is asynchronous. */
		struct BufferInfo
		{
			Rendering::Backend::ResourceHandle<Rendering::Backend::HwBufferObject> handle;
			uint32_t offset = 0;
			uint32_t size = 0;
			BufferType type = BufferType::Uniform;
		};

		/** A texture descriptor, split the way Filament's descriptor sets are: the front end knows
		 * the *handle* (a texture the render side owns, resolved with
		 * Backend::GetHandleAllocator().HandleCast), the back end knows the object the draw
		 * binds. `direct` is for textures that are not an asset - the engine's 1x1 fallbacks and
		 * a target's own attachments - which have no Resources::Texture behind them. `asset` is
		 * the serializable object behind the handle (a .mat stores a path, not a GL name). */
		struct TextureInfo
		{
			Rendering::Backend::ResourceHandle<Rendering::Backend::HwTexture> handle;
			Rendering::Data::Texture* direct = nullptr;
			Rendering::Resources::Texture* asset = nullptr;
		};

		Descriptor() = default;
		Descriptor(const Descriptor&) = delete;
		Descriptor& operator=(const Descriptor&) = delete;
		Descriptor(Descriptor&& p_other) noexcept = default;
		Descriptor& operator=(Descriptor&& p_other) noexcept = default;

		static Descriptor MakeBuffer(
			BufferType p_type,
			Rendering::Backend::ResourceHandle<Rendering::Backend::HwBufferObject> p_buffer,
			uint32_t p_offset,
			uint32_t p_size);

		/** A texture descriptor from the front end's texture, or from the asset that stands for
		 * one (the loader's own textures are named by a Resources::Texture). */
		static Descriptor MakeTexture(
			Rendering::Data::Texture* p_texture,
			Rendering::Resources::Texture* p_asset = nullptr);

		Type GetType() const { return static_cast<Type>(m_value.index()); }
		bool IsValid() const;
		bool IsBuffer() const { return GetType() == Type::Buffer; }
		bool IsTexture() const { return GetType() == Type::Texture; }

		const BufferInfo* GetBuffer() const;
		const TextureInfo* GetTexture() const;

		Descriptor::BufferType GetBufferType() const;

		/** Binds the wrapped resource at p_slot (the descriptor array index). */
		void Bind(uint32_t p_slot) const;

		/** Uploads CPU bytes into the buffer range (glNamedBufferSubData). */
		void Upload(const void* p_data, uint32_t p_size) const;

	private:
		using Value = std::variant<std::monostate, BufferInfo, TextureInfo>;
		Value m_value;
	};
}
