#include <glad/glad.h>

#include <Rendering/Data/Descriptor.h>

namespace Rendering::Data
{
	Descriptor::~Descriptor()
	{
		Release();
	}

	Descriptor::Descriptor(Descriptor&& p_other) noexcept
		: m_value(std::move(p_other.m_value))
	{
		p_other.m_value = std::monostate{};
	}

	Descriptor& Descriptor::operator=(Descriptor&& p_other) noexcept
	{
		if (this != &p_other)
		{
			Release();
			m_value = std::move(p_other.m_value);
			p_other.m_value = std::monostate{};
		}
		return *this;
	}

	Descriptor Descriptor::MakeBuffer(UniformBufferPool::Slice p_slice)
	{
		Descriptor descriptor;
		descriptor.m_value = BufferBinding{ p_slice };
		return descriptor;
	}

	Descriptor Descriptor::MakeTexture(
		Rendering::HAL::TextureHandle* p_handle,
		Rendering::Resources::Texture* p_asset)
	{
		Descriptor descriptor;
		descriptor.m_value = TextureBinding{ p_handle, p_asset };
		return descriptor;
	}

	Descriptor Descriptor::MakeUniform(std::string p_name, const MaterialPropertyType& p_value)
	{
		Descriptor descriptor;
		descriptor.m_value = UniformBinding{ std::move(p_name), p_value };
		return descriptor;
	}

	bool Descriptor::IsValid() const
	{
		if (const BufferBinding* buffer = GetBuffer())
		{
			return buffer->slice.IsValid();
		}
		if (const TextureBinding* texture = GetTexture())
		{
			return texture->handle != nullptr;
		}
		if (const UniformBinding* uniform = GetUniform())
		{
			return !uniform->name.empty()
				&& !std::holds_alternative<std::monostate>(uniform->value);
		}
		return false;
	}

	const Descriptor::BufferBinding* Descriptor::GetBuffer() const
	{
		return std::get_if<BufferBinding>(&m_value);
	}

	const Descriptor::TextureBinding* Descriptor::GetTexture() const
	{
		return std::get_if<TextureBinding>(&m_value);
	}

	const Descriptor::UniformBinding* Descriptor::GetUniform() const
	{
		return std::get_if<UniformBinding>(&m_value);
	}

	Descriptor::UniformBinding* Descriptor::GetUniform()
	{
		return std::get_if<UniformBinding>(&m_value);
	}

	void Descriptor::Bind(uint32_t p_binding) const
	{
		if (const BufferBinding* buffer = GetBuffer())
		{
			UniformBufferPool::Get().Bind(buffer->slice, p_binding);
			return;
		}

		if (const TextureBinding* texture = GetTexture())
		{
			// A material property can hold a handle whose texture was deleted (a resource
			// reloaded while an instance still points at the old one). Binding a stale name is
			// a GL error on every draw; skip it instead.
			if (texture->handle != nullptr
				&& texture->handle->GetID() != 0
				&& glIsTexture(texture->handle->GetID()))
			{
				texture->handle->Bind(p_binding);
			}
		}
	}

	void Descriptor::Release()
	{
		if (BufferBinding* buffer = std::get_if<BufferBinding>(&m_value))
		{
			if (buffer->slice.IsValid())
			{
				UniformBufferPool::Get().Release(buffer->slice);
			}
			m_value = std::monostate{};
		}
	}
}
