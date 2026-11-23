#pragma once
#include <cstdint>
#include <variant>

#include <Rendering/Backend/ResourceHandle.h>
#include <Rendering/HAL/Texture.h>
#include <Rendering/HAL/TextureHandle.h>

namespace Rendering::Resources
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

		struct BufferInfo
		{
			uint32_t bufferId = 0;   // GL buffer name
			uint32_t offset = 0;
			uint32_t size = 0;
			BufferType type = BufferType::Uniform;
		};

		/** A texture descriptor, split the way Filament's descriptor sets are: the front end knows
		 * the *handle* (a texture the render side owns, resolved with
		 * Backend::GetHandleAllocator().HandleCast), the back end knows the object the draw
		 * binds. `direct` is for resources that only exist on the backend side - render-target
		 * attachments and the engine's 1x1 fallbacks - which never come from the asset side.
		 * `asset` is the serializable object behind the handle (a .mat stores a path, not a GL
		 * name). */
		struct TextureInfo
		{
			Rendering::Backend::ResourceHandle<Rendering::HAL::Texture> handle;
			Rendering::HAL::TextureHandle* direct = nullptr;
			Rendering::Resources::Texture* asset = nullptr;
		};

		Descriptor() = default;
		Descriptor(const Descriptor&) = delete;
		Descriptor& operator=(const Descriptor&) = delete;
		Descriptor(Descriptor&& p_other) noexcept = default;
		Descriptor& operator=(Descriptor&& p_other) noexcept = default;

		static Descriptor MakeBuffer(
			BufferType p_type,
			uint32_t p_bufferId,
			uint32_t p_offset,
			uint32_t p_size);
		static Descriptor MakeTexture(
			Rendering::HAL::TextureHandle* p_handle,
			Rendering::Resources::Texture* p_asset = nullptr);

		/** Commit: turns a front-end texture handle into the backend object the draw binds, in
		 * place. Filament does the same when a descriptor set is committed. */
		void ResolveTexture();

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
