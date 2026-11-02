#include <algorithm>

#include <glad/glad.h>

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

	std::vector<uint8_t>& DescriptorSet::Buffer(uint32_t p_size, uint32_t p_binding)
	{
		m_bufferBinding = p_binding;

		// A hot-reloaded shader can change the block size; the pool slice has to match it or
		// glBufferSubData would write outside the range.
		if (m_bufferSlice.size != p_size)
		{
			ReleaseBuffer();
			m_bufferSlice = UniformBufferPool::Get().Acquire(p_size);
		}
		if (m_data.size() != p_size)
		{
			m_data.assign(p_size, 0);
		}

		m_bufferDirty = true;
		return m_data;
	}

	bool DescriptorSet::SetTexture(uint32_t p_binding, Rendering::HAL::TextureHandle* p_texture)
	{
		// A set without a layout is a source-built shader's: there is no contract to check.
		if (m_layout != nullptr && !m_layout->Validate(DescriptorSetLayout::Kind::Texture, p_binding))
		{
			return false;
		}

		const auto it = std::find_if(m_textures.begin(), m_textures.end(),
			[p_binding](const auto& p_entry) { return p_entry.first == p_binding; });
		if (it != m_textures.end())
		{
			it->second = p_texture;
		}
		else
		{
			m_textures.emplace_back(p_binding, p_texture);
		}
		return true;
	}

	Rendering::HAL::TextureHandle* DescriptorSet::GetTexture(uint32_t p_binding) const
	{
		const auto it = std::find_if(m_textures.begin(), m_textures.end(),
			[p_binding](const auto& p_entry) { return p_entry.first == p_binding; });
		return it != m_textures.end() ? it->second : nullptr;
	}

	void DescriptorSet::Commit()
	{
		if (!m_bufferDirty || !m_bufferSlice.IsValid() || m_data.empty())
		{
			return;
		}

		UniformBufferPool::Get().Write(m_bufferSlice, m_data.data());
		m_bufferDirty = false;
	}

	void DescriptorSet::Bind() const
	{
		if (m_bufferSlice.IsValid())
		{
			UniformBufferPool::Get().Bind(m_bufferSlice, m_bufferBinding);
		}

		for (const auto& [binding, texture] : m_textures)
		{
			// A material property can hold a handle whose texture was deleted (a resource
			// reloaded while an instance still points at the old one). Binding a stale name is
			// a GL error on every draw; skip it instead.
			if (texture != nullptr && texture->GetID() != 0 && glIsTexture(texture->GetID()))
			{
				texture->Bind(binding);
			}
		}
	}

	void DescriptorSet::Release()
	{
		ReleaseBuffer();
		m_textures.clear();
		m_layout = nullptr;
	}

	void DescriptorSet::ReleaseBuffer()
	{
		if (m_bufferSlice.IsValid())
		{
			UniformBufferPool::Get().Release(m_bufferSlice);
			m_bufferSlice = {};
		}
		m_data.clear();
		m_bufferDirty = false;
	}
}

