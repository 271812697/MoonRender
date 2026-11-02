#pragma once
#include <cstdint>
#include <vector>

#include <Rendering/Data/Descriptor.h>
#include <Rendering/Data/DescriptorSetLayout.h>
#include <Rendering/HAL/TextureHandle.h>

/** The PerMaterial binding set: the actual resources a material instance binds against its
 * DescriptorSetLayout - one uniform-buffer range plus one texture per sampler.
 *
 * This is the runtime half of filament's DescriptorSet (filament/src/ds/DescriptorSet.h):
 * a layout says which slot takes what, and this object holds what is actually in the slots.
 * Each slot is a Descriptor (a wrapped buffer range or texture), never a raw pointer; the set
 * validates the kind against the layout, so a texture cannot land in the block's slot.
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
		const std::vector<uint8_t>& BufferData() const { return m_data; }
		bool HasBuffer() const;

		/** Records p_texture at p_binding, after checking the binding against the layout.
		 * A null texture clears the slot. Returns false when the layout has no such slot. */
		bool SetTexture(
			uint32_t p_binding,
			Rendering::HAL::TextureHandle* p_texture,
			Rendering::Resources::Texture* p_asset = nullptr);
		Rendering::HAL::TextureHandle* GetTexture(uint32_t p_binding) const;
		Rendering::Resources::Texture* GetTextureAsset(uint32_t p_binding) const;

		/** Loose uniforms of a shader built without a package: the value lives in a Descriptor
		 * and is applied to the program on bind, instead of a per-instance property map. */
		void SetUniform(const std::string& p_name, const MaterialPropertyType& p_value);
		const Descriptor::UniformBinding* GetUniform(const std::string& p_name) const;
		void RemoveUniform(const std::string& p_name);
		void ClearUniforms() { m_uniforms.clear(); }
		const std::vector<Descriptor>& Uniforms() const { return m_uniforms; }

		/** Uploads the block range when it changed. No-op for an empty set. */
		void Commit();

		/** Binds the block range and every recorded texture. Called on every draw, because
		 * another material's bind may have replaced them since the last one. */
		void Bind() const;

		/** Returns the block range to the pool and forgets every binding. */
		void Release();

	private:
		void EnsureBufferSize(uint32_t p_binding);
		void EnsureTextureSize(uint32_t p_binding);
		int FindBufferBinding() const;

		const DescriptorSetLayout* m_layout = nullptr;
		uint32_t m_bufferBinding = 0;
		std::vector<uint8_t> m_data;
		bool m_bufferDirty = false;
		/** GL uniform-buffer binding points and texture units are separate namespaces: the
		 * MaterialParams block lives at point 2 while sampler 2 lives at unit 2. Keeping them
		 * in one vector would make the texture overwrite the block. */
		std::vector<Descriptor> m_buffers;
		std::vector<Descriptor> m_textures;
		std::vector<Descriptor> m_uniforms;
	};
}
