#pragma once
#include <cstdint>
#include <vector>

#include <Rendering/Data/Descriptor.h>
#include <Rendering/Data/DescriptorSetLayout.h>
#include <Rendering/HAL/ShaderProgram.h>
#include <Rendering/HAL/TextureHandle.h>

/** One descriptor array per descriptor set. The array index is the shader's original binding
 * point; at bind time the OpenGLProgram binding map turns (set, binding) into the GL slot the
 * shader was initialized with. */
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
			uint32_t p_binding,
			Descriptor::BufferType p_type,
			uint32_t p_bufferId,
			uint32_t p_offset,
			uint32_t p_size);
		bool SetTexture(
			uint32_t p_binding,
			Rendering::HAL::TextureHandle* p_texture,
			Rendering::Resources::Texture* p_asset = nullptr);

		const Descriptor* FindBuffer(uint32_t p_binding, Descriptor::BufferType p_type) const;
		const Descriptor* FindTexture(uint32_t p_binding) const;

		Rendering::HAL::TextureHandle* GetTexture(uint32_t p_binding) const;
		Rendering::Resources::Texture* GetTextureAsset(uint32_t p_binding) const;

		void UploadBuffer(
			uint32_t p_binding,
			Descriptor::BufferType p_type,
			const void* p_data,
			uint32_t p_size);

		/** Binds every descriptor at the slot the program's binding map assigns. */
		void Bind(const Rendering::HAL::ShaderProgram& p_program, uint32_t p_set) const;
		/** Binds a set whose (binding -> GL slot) mapping is fixed by the engine. */
		void BindFixed(uint32_t p_set) const;
		void Release();

		const std::vector<Descriptor>& Descriptors() const { return m_descriptors; }

	private:
		const DescriptorSetLayout* m_layout = nullptr;
		std::vector<Descriptor> m_descriptors;
	};
}
