#include <algorithm>

#include <glad/glad.h>

#include <Rendering/Backend/BackendResources.h>
#include <Rendering/Data/Descriptor.h>

#include <Rendering/Context/Driver.h>
#include <Rendering/HAL/Common/HALDriverAccess.h>
#include <Rendering/Resources/Texture.h>

namespace Rendering::Data
{
	Descriptor Descriptor::MakeBuffer(
		BufferType p_type,
		Rendering::Backend::ResourceHandle<Rendering::Backend::HwBufferObject> p_buffer,
		uint32_t p_offset,
		uint32_t p_size)
	{
		Descriptor descriptor;
		descriptor.m_value = BufferInfo{ p_buffer, p_offset, p_size, p_type };
		return descriptor;
	}

	Descriptor Descriptor::MakeTexture(
		Rendering::Data::Texture* p_texture,
		Rendering::Resources::Texture* p_asset)
	{
		Descriptor descriptor;
		TextureInfo info;
		info.direct = p_texture;
		info.asset = p_asset;
		// The handle is what every command names: the front end's own when there is one, otherwise
		// the asset's (the loader's textures are named by their Resources::Texture, which holds the
		// front end).
		if (p_texture != nullptr)
		{
			info.handle = p_texture->GetHandle();
		}
		else if (p_asset != nullptr)
		{
			info.handle = p_asset->GetHandle();
		}
		descriptor.m_value = info;
		return descriptor;
	}

	bool Descriptor::IsValid() const
	{
		if (const BufferInfo* buffer = GetBuffer())
		{
			return static_cast<bool>(buffer->handle) && buffer->size > 0;
		}
		if (const TextureInfo* texture = GetTexture())
		{
			return static_cast<bool>(texture->handle) || texture->direct != nullptr;
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
		// The bind is a command whenever there is a driver to record into (Filament's
		// bindDescriptorSet): the GL call belongs to the batch that binds the drawable, and the
		// recording side must not issue it - with a render thread there is no context there, and
		// without one the pending batch would still be ahead of it (docs/RenderThreadPlan.md §8.72).
		::Rendering::Context::Driver* const driver = ::Rendering::HAL::CurrentDriver();
		const bool viaCommand = driver != nullptr;

		if (const BufferInfo* buffer = GetBuffer())
		{
			if (viaCommand)
			{
				::Rendering::Backend::BindDescriptorRequest request;
				request.kind = buffer->type == BufferType::ShaderStorage
					? ::Rendering::Backend::BindDescriptorRequest::Kind::ShaderStorageBuffer
					: ::Rendering::Backend::BindDescriptorRequest::Kind::UniformBuffer;
				request.handleId = buffer->handle.GetId();
				request.slot = p_slot;
				request.offset = buffer->offset;
				request.size = buffer->size;
				driver->BindDescriptor(request);
				return;
			}

			const uint32_t name = ::Rendering::Backend::Records::BufferName(buffer->handle);
			if (name == 0)
			{
				return;
			}
			const GLenum target = buffer->type == BufferType::ShaderStorage
				? GL_SHADER_STORAGE_BUFFER : GL_UNIFORM_BUFFER;
			glBindBufferRange(target, p_slot, name, buffer->offset, buffer->size);
			return;
		}

		if (const TextureInfo* texture = GetTexture())
		{
			// The whole descriptor is named by the handle the recording side stored: the executor
			// resolves it to the GL name, so nothing here needs the object.
			const uint32_t handleId = static_cast<bool>(texture->handle)
				? texture->handle.GetId()
				: (texture->direct != nullptr ? texture->direct->GetHandle().GetId() : 0);

			// A descriptor can still name a texture whose object was deleted (a resource reloaded
			// while an instance still points at the old one). Binding a stale name is a GL error
			// on every draw; skip it instead.
			if (viaCommand)
			{
				if (handleId == 0)
				{
					return;
				}

				::Rendering::Backend::BindDescriptorRequest request;
				request.kind = ::Rendering::Backend::BindDescriptorRequest::Kind::Texture;
				request.handleId = handleId;
				request.slot = p_slot;
				driver->BindDescriptor(request);
				return;
			}

			// No driver: this is the side that owns the context, so the name is here.
			const uint32_t name = ::Rendering::Backend::Records::TextureName(handleId);
			if (name != 0 && glIsTexture(name))
			{
				glBindTextureUnit(p_slot, name);
			}
		}
	}

	void Descriptor::Upload(const void* p_data, uint32_t p_size) const
	{
		const BufferInfo* buffer = GetBuffer();
		if (buffer == nullptr || !buffer->handle || p_data == nullptr)
		{
			return;
		}
		const uint32_t size = std::min(p_size, buffer->size);

		// An upload is a command (Filament's updateBufferObject): the recording side only names the
		// range and the buffer's handle, the executor resolves the name and writes it - in both
		// configurations, so the bytes land in the same place in the batch.
		::Rendering::Context::Driver* const driver = ::Rendering::HAL::CurrentDriver();
		if (driver != nullptr)
		{
			driver->UpdateBufferRange(buffer->handle.GetId(), buffer->offset, p_data, size);
			return;
		}

		const uint32_t name = ::Rendering::Backend::Records::BufferName(buffer->handle);
		if (name == 0)
		{
			return;
		}
		glNamedBufferSubData(name, buffer->offset, size, p_data);
	}
}
