#pragma once
#include <cstdint>
#include <variant>

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
			uint32_t binding = 0;
			uint32_t bufferId = 0;   // GL buffer name
			uint32_t offset = 0;
			uint32_t size = 0;
			BufferType type = BufferType::Uniform;
		};

		struct TextureInfo
		{
			uint32_t binding = 0;
			Rendering::HAL::TextureHandle* handle = nullptr;
			/** The serializable asset behind the handle, when there is one (a .mat stores a
			 * path, not a GL name). Null for view-owned textures (DFG LUT, shadows, IBL). */
			Rendering::Resources::Texture* asset = nullptr;
		};

		Descriptor() = default;
		Descriptor(const Descriptor&) = delete;
		Descriptor& operator=(const Descriptor&) = delete;
		Descriptor(Descriptor&& p_other) noexcept = default;
		Descriptor& operator=(Descriptor&& p_other) noexcept = default;

		static Descriptor MakeBuffer(
			BufferType p_type,
			uint32_t p_binding,
			uint32_t p_bufferId,
			uint32_t p_offset,
			uint32_t p_size);
		static Descriptor MakeTexture(
			uint32_t p_binding,
			Rendering::HAL::TextureHandle* p_handle,
			Rendering::Resources::Texture* p_asset = nullptr);

		Type GetType() const { return static_cast<Type>(m_value.index()); }
		bool IsValid() const;
		bool IsBuffer() const { return GetType() == Type::Buffer; }
		bool IsTexture() const { return GetType() == Type::Texture; }

		const BufferInfo* GetBuffer() const;
		const TextureInfo* GetTexture() const;

		uint32_t Binding() const;
		Descriptor::BufferType GetBufferType() const;

		/** Binds the wrapped resource at its own binding point. */
		void Bind() const;

		/** Uploads CPU bytes into the buffer range (glNamedBufferSubData). */
		void Upload(const void* p_data, uint32_t p_size) const;

	private:
		using Value = std::variant<std::monostate, BufferInfo, TextureInfo>;
		Value m_value;
	};
}
