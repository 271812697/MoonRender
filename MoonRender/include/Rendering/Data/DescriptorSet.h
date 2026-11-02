#pragma once
#include <cstdint>
#include <utility>
#include <vector>

#include <Rendering/Data/DescriptorSetLayout.h>
#include <Rendering/Data/UniformBufferPool.h>
#include <Rendering/HAL/TextureHandle.h>

/** The PerMaterial binding set: the actual resources a material instance binds against its
 * DescriptorSetLayout - one uniform-buffer range plus one texture per sampler.
 *
 * This is the runtime half of filament's DescriptorSet (filament/src/ds/DescriptorSet.h):
 * a layout says which slot takes what, and this object holds what is actually in the slots.
 * SetTexture validates the slot against the layout, so a texture can never land in the block's
 * slot; Bind() binds the whole set (glBindBufferRange for the block, glBindTextureUnit for each
 * texture) in one call, instead of the renderer re-binding by name.
 *
 * The CPU mirror of the uniform block lives here too. A material instance writes its parameter
 * values into Buffer(), marks it dirty, calls Commit() once after the write, and the upload is
 * skipped when nothing changed. */
namespace Rendering::Data
{
	class DescriptorSet
	{
	public:
		DescriptorSet() = default;
		~DescriptorSet();

		DescriptorSet(const DescriptorSet&) = delete;
		DescriptorSet& operator=(const DescriptorSet&) = delete;

		/** The layout the set is filled against. Owned by the material definition; this object
		 * only keeps the pointer. */
		void SetLayout(const DescriptorSetLayout* p_layout);
		const DescriptorSetLayout* Layout() const { return m_layout; }

		/** The CPU mirror of the uniform block, grown to p_size and addressed at p_binding.
		 * The caller writes the values by offset and calls Commit() when done. */
		std::vector<uint8_t>& Buffer(uint32_t p_size, uint32_t p_binding);
		uint32_t BufferSize() const { return static_cast<uint32_t>(m_data.size()); }
		bool HasBuffer() const { return m_bufferSlice.IsValid(); }

		/** Records p_texture at p_binding, after checking the binding against the layout.
		 * A null texture clears the slot. Returns false when the layout has no such slot. */
		bool SetTexture(uint32_t p_binding, Rendering::HAL::TextureHandle* p_texture);
		Rendering::HAL::TextureHandle* GetTexture(uint32_t p_binding) const;

		/** Uploads the block range when it changed. No-op for an empty set. */
		void Commit();

		/** Binds the block range and every recorded texture. Called on every draw, because
		 * another material's bind may have replaced them since the last one. */
		void Bind() const;

		/** Returns the block range to the pool and forgets every binding. */
		void Release();

	private:
		void ReleaseBuffer();

		const DescriptorSetLayout* m_layout = nullptr;
		uint32_t m_bufferBinding = 0;
		UniformBufferPool::Slice m_bufferSlice;
		std::vector<uint8_t> m_data;
		bool m_bufferDirty = false;
		std::vector<std::pair<uint32_t, Rendering::HAL::TextureHandle*>> m_textures;
	};
}

