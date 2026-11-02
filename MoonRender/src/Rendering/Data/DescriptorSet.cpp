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

	void DescriptorSet::EnsureBufferSize(uint32_t p_binding)
	{
		if (m_buffers.size() <= p_binding)
		{
			m_buffers.resize(p_binding + 1);
		}
	}

	void DescriptorSet::EnsureTextureSize(uint32_t p_binding)
	{
		if (m_textures.size() <= p_binding)
		{
			m_textures.resize(p_binding + 1);
		}
	}

	std::vector<uint8_t>& DescriptorSet::Buffer(uint32_t p_size, uint32_t p_binding)
	{
		EnsureBufferSize(p_binding);
		m_bufferBinding = p_binding;

		Descriptor& descriptor = m_buffers[p_binding];
		const bool sizeChanged = !descriptor.IsBuffer()
			|| descriptor.GetBuffer()->slice.size != p_size;

		// A hot-reloaded shader can change the block size; the pool slice has to match it or
		// glBufferSubData would write outside the range.
		if (sizeChanged)
		{
			descriptor.Release();
			descriptor = Descriptor::MakeBuffer(UniformBufferPool::Get().Acquire(p_size));
		}
		if (m_data.size() != p_size)
		{
			m_data.assign(p_size, 0);
		}

		m_bufferDirty = true;
		return m_data;
	}

	int DescriptorSet::FindBufferBinding() const
	{
		if (m_bufferBinding < m_buffers.size() && m_buffers[m_bufferBinding].IsBuffer())
		{
			return static_cast<int>(m_bufferBinding);
		}
		for (uint32_t i = 0; i < m_buffers.size(); ++i)
		{
			if (m_buffers[i].IsBuffer())
			{
				return static_cast<int>(i);
			}
		}
		return -1;
	}

	bool DescriptorSet::HasBuffer() const
	{
		return FindBufferBinding() >= 0;
	}

	bool DescriptorSet::SetTexture(
		uint32_t p_binding,
		Rendering::HAL::TextureHandle* p_texture,
		Rendering::Resources::Texture* p_asset)
	{
		// A set without a layout is a source-built shader's: there is no contract to check.
		if (m_layout != nullptr && !m_layout->Validate(DescriptorSetLayout::Kind::Texture, p_binding))
		{
			return false;
		}

		EnsureTextureSize(p_binding);
		Descriptor& descriptor = m_textures[p_binding];
		descriptor.Release();
		descriptor = p_texture != nullptr
			? Descriptor::MakeTexture(p_texture, p_asset)
			: Descriptor{};
		return true;
	}

	Rendering::HAL::TextureHandle* DescriptorSet::GetTexture(uint32_t p_binding) const
	{
		if (p_binding >= m_textures.size())
		{
			return nullptr;
		}
		const Descriptor::TextureBinding* texture = m_textures[p_binding].GetTexture();
		return texture != nullptr ? texture->handle : nullptr;
	}

	Rendering::Resources::Texture* DescriptorSet::GetTextureAsset(uint32_t p_binding) const
	{
		if (p_binding >= m_textures.size())
		{
			return nullptr;
		}
		const Descriptor::TextureBinding* texture = m_textures[p_binding].GetTexture();
		return texture != nullptr ? texture->asset : nullptr;
	}

	void DescriptorSet::SetUniform(const std::string& p_name, const MaterialPropertyType& p_value)
	{
		for (Descriptor& descriptor : m_uniforms)
		{
			if (Descriptor::UniformBinding* uniform = descriptor.GetUniform();
				uniform != nullptr && uniform->name == p_name)
			{
				uniform->value = p_value;
				return;
			}
		}
		m_uniforms.push_back(Descriptor::MakeUniform(p_name, p_value));
	}

	const Descriptor::UniformBinding* DescriptorSet::GetUniform(const std::string& p_name) const
	{
		for (const Descriptor& descriptor : m_uniforms)
		{
			if (const Descriptor::UniformBinding* uniform = descriptor.GetUniform();
				uniform != nullptr && uniform->name == p_name)
			{
				return uniform;
			}
		}
		return nullptr;
	}

	void DescriptorSet::RemoveUniform(const std::string& p_name)
	{
		std::erase_if(m_uniforms, [&p_name](const Descriptor& p_descriptor) {
			const Descriptor::UniformBinding* uniform = p_descriptor.GetUniform();
			return uniform != nullptr && uniform->name == p_name;
		});
	}

	void DescriptorSet::Commit()
	{
		const int bufferBinding = FindBufferBinding();
		if (!m_bufferDirty || bufferBinding < 0 || m_data.empty())
		{
			return;
		}

		UniformBufferPool::Get().Write(m_buffers[bufferBinding].GetBuffer()->slice, m_data.data());
		m_bufferDirty = false;
	}

	void DescriptorSet::Bind() const
	{
		for (uint32_t binding = 0; binding < m_buffers.size(); ++binding)
		{
			if (m_buffers[binding].IsValid())
			{
				m_buffers[binding].Bind(binding);
			}
		}
		for (uint32_t binding = 0; binding < m_textures.size(); ++binding)
		{
			if (m_textures[binding].IsValid())
			{
				m_textures[binding].Bind(binding);
			}
		}
	}

	void DescriptorSet::Release()
	{
		for (Descriptor& descriptor : m_buffers)
		{
			descriptor.Release();
		}
		for (Descriptor& descriptor : m_textures)
		{
			descriptor.Release();
		}
		m_buffers.clear();
		m_textures.clear();
		m_uniforms.clear();
		m_data.clear();
		m_bufferDirty = false;
		m_bufferBinding = 0;
		m_layout = nullptr;
	}
}
