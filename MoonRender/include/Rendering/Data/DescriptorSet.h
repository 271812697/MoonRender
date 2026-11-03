#pragma once
#include <cstdint>
#include <vector>

#include <Rendering/Data/Descriptor.h>
#include <Rendering/Data/DescriptorSetLayout.h>
#include <Rendering/HAL/TextureHandle.h>

/** The PerMaterial binding set: one array of GPU descriptors.
 *
 * Each Descriptor carries its own binding point and kind (UBO/SSBO/texture). A GL uniform
 * buffer binding point and a texture unit are different namespaces, so the same number (e.g.
 * the MaterialParams block at 2 and sampler 2) can appear twice in the array; the descriptor
 * itself says which namespace it belongs to. The set holds no CPU uniform data - the material
 * instance owns that and uploads it through UploadBuffer() before Bind(). */
namespace Rendering::Data
{
	class DescriptorSet
	{
	public:
		DescriptorSet() = default;
		~DescriptorSet();

		DescriptorSet(const DescriptorSet&) = delete;
		DescriptorSet& operator=(const DescriptorSet&) = delete;

		void SetLayout(const DescriptorSetLayout* p_layout);
		const DescriptorSetLayout* Layout() const { return m_layout; }

		bool SetBuffer(
			Descriptor::BufferType p_type,
			uint32_t p_binding,
			uint32_t p_bufferId,
			uint32_t p_offset,
			uint32_t p_size);
		bool SetTexture(
			uint32_t p_binding,
			Rendering::HAL::TextureHandle* p_texture,
			Rendering::Resources::Texture* p_asset = nullptr);

		const Descriptor* FindBuffer(
			uint32_t p_binding,
			Descriptor::BufferType p_type) const;
		const Descriptor* FindTexture(uint32_t p_binding) const;

		Rendering::HAL::TextureHandle* GetTexture(uint32_t p_binding) const;
		Rendering::Resources::Texture* GetTextureAsset(uint32_t p_binding) const;

		/** Uploads CPU uniform bytes into the buffer descriptor's GL range. */
		void UploadBuffer(
			uint32_t p_binding,
			Descriptor::BufferType p_type,
			const void* p_data,
			uint32_t p_size);

		void Bind() const;
		void Release();

		const std::vector<Descriptor>& Descriptors() const { return m_descriptors; }

	private:
		Descriptor* FindBufferMutable(uint32_t p_binding, Descriptor::BufferType p_type);
		Descriptor* FindTextureMutable(uint32_t p_binding);

		const DescriptorSetLayout* m_layout = nullptr;
		std::vector<Descriptor> m_descriptors;
	};
}
