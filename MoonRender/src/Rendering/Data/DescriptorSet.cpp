#include <algorithm>

#include <Rendering/Data/DescriptorSet.h>

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

	Descriptor* DescriptorSet::FindBufferMutable(
		uint32_t p_binding,
		Descriptor::BufferType p_type)
	{
		for (Descriptor& descriptor : m_descriptors)
		{
			if (const Descriptor::BufferInfo* buffer = descriptor.GetBuffer();
				buffer != nullptr && buffer->binding == p_binding && buffer->type == p_type)
			{
				return &descriptor;
			}
		}
		return nullptr;
	}

	Descriptor* DescriptorSet::FindTextureMutable(uint32_t p_binding)
	{
		for (Descriptor& descriptor : m_descriptors)
		{
			if (const Descriptor::TextureInfo* texture = descriptor.GetTexture();
				texture != nullptr && texture->binding == p_binding)
			{
				return &descriptor;
			}
		}
		return nullptr;
	}

	bool DescriptorSet::SetBuffer(
		Descriptor::BufferType p_type,
		uint32_t p_binding,
		uint32_t p_bufferId,
		uint32_t p_offset,
		uint32_t p_size)
	{
		if (m_layout != nullptr && p_type == Descriptor::BufferType::Uniform
			&& !m_layout->Validate(DescriptorSetLayout::Kind::UniformBuffer, p_binding))
		{
			return false;
		}

		Descriptor* existing = FindBufferMutable(p_binding, p_type);
		if (existing != nullptr)
		{
			*existing = Descriptor::MakeBuffer(p_type, p_binding, p_bufferId, p_offset, p_size);
		}
		else
		{
			m_descriptors.push_back(
				Descriptor::MakeBuffer(p_type, p_binding, p_bufferId, p_offset, p_size));
		}
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

		Descriptor* existing = FindTextureMutable(p_binding);
		if (p_texture == nullptr)
		{
			if (existing != nullptr)
			{
				m_descriptors.erase(std::remove_if(m_descriptors.begin(), m_descriptors.end(),
					[p_binding](const Descriptor& p_descriptor) {
						const Descriptor::TextureInfo* texture = p_descriptor.GetTexture();
						return texture != nullptr && texture->binding == p_binding;
					}), m_descriptors.end());
			}
		}
		else if (existing != nullptr)
		{
			*existing = Descriptor::MakeTexture(p_binding, p_texture, p_asset);
		}
		else
		{
			m_descriptors.push_back(
				Descriptor::MakeTexture(p_binding, p_texture, p_asset));
		}
		return true;
	}

	const Descriptor* DescriptorSet::FindBuffer(
		uint32_t p_binding,
		Descriptor::BufferType p_type) const
	{
		for (const Descriptor& descriptor : m_descriptors)
		{
			if (const Descriptor::BufferInfo* buffer = descriptor.GetBuffer();
				buffer != nullptr && buffer->binding == p_binding && buffer->type == p_type)
			{
				return &descriptor;
			}
		}
		return nullptr;
	}

	const Descriptor* DescriptorSet::FindTexture(uint32_t p_binding) const
	{
		for (const Descriptor& descriptor : m_descriptors)
		{
			if (const Descriptor::TextureInfo* texture = descriptor.GetTexture();
				texture != nullptr && texture->binding == p_binding)
			{
				return &descriptor;
			}
		}
		return nullptr;
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
		Descriptor* descriptor = FindBufferMutable(p_binding, p_type);
		if (descriptor != nullptr)
		{
			descriptor->Upload(p_data, p_size);
		}
	}

	void DescriptorSet::Bind() const
	{
		for (const Descriptor& descriptor : m_descriptors)
		{
			if (descriptor.IsValid())
			{
				descriptor.Bind();
			}
		}
	}

	void DescriptorSet::Release()
	{
		m_descriptors.clear();
		m_layout = nullptr;
	}
}
