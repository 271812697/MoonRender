#include <Rendering/Data/DescriptorSet.h>

#include <cstdlib>
#include <iostream>
#include <limits>

namespace Rendering::Data
{
	DescriptorSet::~DescriptorSet()
	{
		Release();
	}

	void DescriptorSet::SetLayout(const DescriptorSetLayout* p_layout)
	{
		m_layout = p_layout;
	}

	bool DescriptorSet::SetBuffer(
		uint32_t p_binding,
		Descriptor::BufferType p_type,
		uint32_t p_bufferId,
		uint32_t p_offset,
		uint32_t p_size)
	{
		if (m_layout != nullptr
			&& !m_layout->Validate(DescriptorSetLayout::Kind::UniformBuffer, p_binding)
			&& !m_layout->Validate(DescriptorSetLayout::Kind::StorageBuffer, p_binding))
		{
			return false;
		}

		if (p_binding >= m_descriptors.size())
		{
			m_descriptors.resize(p_binding + 1);
		}
		m_descriptors[p_binding] =
			Descriptor::MakeBuffer(p_type, p_bufferId, p_offset, p_size);
		return true;
	}

	bool DescriptorSet::SetTexture(
		uint32_t p_binding,
		Rendering::HAL::TextureHandle* p_texture,
		Rendering::Resources::Texture* p_asset)
	{
		if (m_layout != nullptr
			&& !m_layout->Validate(DescriptorSetLayout::Kind::Texture, p_binding))
		{
			return false;
		}

		if (p_binding >= m_descriptors.size())
		{
			m_descriptors.resize(p_binding + 1);
		}
		m_descriptors[p_binding] = p_texture != nullptr
			? Descriptor::MakeTexture(p_texture, p_asset)
			: Descriptor{};
		return true;
	}

	const Descriptor* DescriptorSet::FindBuffer(
		uint32_t p_binding,
		Descriptor::BufferType p_type) const
	{
		if (p_binding >= m_descriptors.size())
		{
			return nullptr;
		}
		const Descriptor& descriptor = m_descriptors[p_binding];
		return descriptor.IsBuffer() && descriptor.GetBufferType() == p_type
			? &descriptor : nullptr;
	}

	const Descriptor* DescriptorSet::FindTexture(uint32_t p_binding) const
	{
		if (p_binding >= m_descriptors.size() || !m_descriptors[p_binding].IsTexture())
		{
			return nullptr;
		}
		return &m_descriptors[p_binding];
	}

	Rendering::HAL::TextureHandle* DescriptorSet::GetTexture(uint32_t p_binding) const
	{
		const Descriptor* descriptor = FindTexture(p_binding);
		const Descriptor::TextureInfo* texture =
			descriptor != nullptr ? descriptor->GetTexture() : nullptr;
		return texture != nullptr ? texture->handle : nullptr;
	}

	Rendering::Resources::Texture* DescriptorSet::GetTextureAsset(uint32_t p_binding) const
	{
		const Descriptor* descriptor = FindTexture(p_binding);
		const Descriptor::TextureInfo* texture =
			descriptor != nullptr ? descriptor->GetTexture() : nullptr;
		return texture != nullptr ? texture->asset : nullptr;
	}

	void DescriptorSet::UploadBuffer(
		uint32_t p_binding,
		Descriptor::BufferType p_type,
		const void* p_data,
		uint32_t p_size)
	{
		if (p_binding >= m_descriptors.size())
		{
			return;
		}
		Descriptor& descriptor = m_descriptors[p_binding];
		if (descriptor.IsBuffer() && descriptor.GetBufferType() == p_type)
		{
			descriptor.Upload(p_data, p_size);
		}
	}

	void DescriptorSet::Bind(
		const Rendering::HAL::ShaderProgram& p_program,
		uint32_t p_set) const
	{
		for (uint32_t binding = 0; binding < m_descriptors.size(); ++binding)
		{
			const Descriptor& descriptor = m_descriptors[binding];
			if (!descriptor.IsValid())
			{
				continue;
			}
			const uint32_t slot = p_program.GetDescriptorSlot(p_set, binding);
			if (slot != std::numeric_limits<uint32_t>::max())
			{
				if (std::getenv("MOON_DEBUG_BINDINGS") != nullptr)
				{
					std::cout << "[DescriptorSet] set " << p_set << " binding " << binding
						<< " -> slot " << slot << " ("
						<< (descriptor.IsBuffer() ? "buffer" : "texture") << ")" << std::endl;
				}
				descriptor.Bind(slot);
			}
		}
	}

	void DescriptorSet::Release()
	{
		m_descriptors.clear();
		m_layout = nullptr;
	}
}
