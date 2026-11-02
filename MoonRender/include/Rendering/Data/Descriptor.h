#pragma once
#include <cstdint>
#include <string>
#include <variant>

#include <Rendering/Data/MaterialProperty.h>
#include <Rendering/Data/UniformBufferPool.h>
#include <Rendering/HAL/TextureHandle.h>

namespace Rendering::Resources
{
	class Texture;
}

/** One slot of a DescriptorSet: a wrapped render resource, never a bare pointer.
 *
 * A descriptor is either a uniform-buffer range (the PerMaterial std140 block) or a texture
 * + sampler pair. DescriptorSet validates the kind against its layout; this class only knows
 * how to bind/release the resource it owns. Filament's DescriptorSet::Desc is the same idea,
 * a tagged union of a buffer handle/range and a texture handle/params. */
namespace Rendering::Data
{
	class Descriptor
	{
	public:
		enum class Kind : uint8_t
		{
			Invalid = 0,
			UniformBuffer = 1,
			Texture = 2,
			Uniform = 3,
		};

		struct BufferBinding
		{
			UniformBufferPool::Slice slice;
		};

		struct TextureBinding
		{
			Rendering::HAL::TextureHandle* handle = nullptr;
			/** The serializable asset behind the handle, when there is one (a .mat stores a
			 * path, not a GL name). Null for view-owned textures (DFG LUT, shadows, IBL). */
			Rendering::Resources::Texture* asset = nullptr;
		};

		struct UniformBinding
		{
			std::string name;
			MaterialPropertyType value;
		};

		Descriptor() = default;
		~Descriptor();

		Descriptor(const Descriptor&) = delete;
		Descriptor& operator=(const Descriptor&) = delete;
		Descriptor(Descriptor&& p_other) noexcept;
		Descriptor& operator=(Descriptor&& p_other) noexcept;

		static Descriptor MakeBuffer(UniformBufferPool::Slice p_slice);
		static Descriptor MakeTexture(
			Rendering::HAL::TextureHandle* p_handle,
			Rendering::Resources::Texture* p_asset = nullptr);
		static Descriptor MakeUniform(std::string p_name, const MaterialPropertyType& p_value);

		Kind GetKind() const { return static_cast<Kind>(m_value.index()); }
		bool IsValid() const;
		bool IsBuffer() const { return GetKind() == Kind::UniformBuffer; }
		bool IsTexture() const { return GetKind() == Kind::Texture; }
		bool IsUniform() const { return GetKind() == Kind::Uniform; }

		const BufferBinding* GetBuffer() const;
		const TextureBinding* GetTexture() const;
		const UniformBinding* GetUniform() const;
		UniformBinding* GetUniform();

		/** Binds the wrapped resource at p_binding (glBindBufferRange or glBindTextureUnit). */
		void Bind(uint32_t p_binding) const;

		/** Returns a buffer range to the pool; textures are not owned. */
		void Release();

	private:
		using Value = std::variant<std::monostate, BufferBinding, TextureBinding, UniformBinding>;
		Value m_value;
	};
}
