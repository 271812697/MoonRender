#include <algorithm>

#include <glad/glad.h>

#include <Rendering/Data/Descriptor.h>

namespace Rendering::Data
{
	Descriptor Descriptor::MakeBuffer(
		BufferType p_type,
		uint32_t p_bufferId,
		uint32_t p_offset,
		uint32_t p_size)
	{
		Descriptor descriptor;
		descriptor.m_value = BufferInfo{ p_bufferId, p_offset, p_size, p_type };
		return descriptor;
	}

	Descriptor Descriptor::MakeTexture(
		Rendering::HAL::TextureHandle* p_handle,
		Rendering::Resources::Texture* p_asset)
	{
		Descriptor descriptor;
		descriptor.m_value = TextureInfo{ p_handle, p_asset };
		return descriptor;
	}

	bool Descriptor::IsValid() const
	{
		if (const BufferInfo* buffer = GetBuffer())
		{
			return buffer->bufferId != 0 && buffer->size > 0;
		}
		if (const TextureInfo* texture = GetTexture())
		{
			return texture->handle != nullptr;
		}
		return false;
	}

	const Descriptor::BufferInfo* Descriptor::GetBuffer() const
	{
		return std::get_if<BufferInfo>(&m_value);
	}

	const Descriptor::TextureInfo* Descriptor::GetTexture() const
	{
		return std::get_if<TextureInfo>(&m_value);
	}

	Descriptor::BufferType Descriptor::GetBufferType() const
	{
		const BufferInfo* buffer = GetBuffer();
		return buffer != nullptr ? buffer->type : BufferType::Uniform;
	}

	void Descriptor::Bind(uint32_t p_slot) const
	{
		if (const BufferInfo* buffer = GetBuffer())
		{
			const GLenum target = buffer->type == BufferType::ShaderStorage
				? GL_SHADER_STORAGE_BUFFER : GL_UNIFORM_BUFFER;
			glBindBufferRange(target, p_slot, buffer->bufferId,
				buffer->offset, buffer->size);
			return;
		}

		if (const TextureInfo* texture = GetTexture())
		{
			// A material property can hold a handle whose texture was deleted (a resource
			// reloaded while an instance still points at the old one). Binding a stale name is
			// a GL error on every draw; skip it instead.
			if (texture->handle != nullptr
				&& texture->handle->GetID() != 0
				&& glIsTexture(texture->handle->GetID()))
			{
				texture->handle->Bind(p_slot);
			}
		}
	}

	void Descriptor::Upload(const void* p_data, uint32_t p_size) const
	{
		const BufferInfo* buffer = GetBuffer();
		if (buffer == nullptr || buffer->bufferId == 0 || p_data == nullptr)
		{
			return;
		}
		glNamedBufferSubData(buffer->bufferId, buffer->offset,
			std::min(p_size, buffer->size), p_data);
	}
}
