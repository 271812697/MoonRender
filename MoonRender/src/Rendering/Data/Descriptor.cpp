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
		Rendering::HAL::TextureHandle* p_handle,
		Rendering::Resources::Texture* p_asset)
	{
		Descriptor descriptor;
		TextureInfo info;
		info.direct = p_handle;
		info.asset = p_asset;
		if (p_asset != nullptr)
		{
			// The front end names the asset by handle; the pointer is only the eager fallback until
			// the resolver below has run (docs/RenderThreadPlan.md §7).
			info.handle = p_asset->GetHandle();
		}
		descriptor.m_value = info;
		return descriptor;
	}

	void Descriptor::ResolveTexture()
	{
		TextureInfo* texture = std::get_if<TextureInfo>(&m_value);
		if (texture == nullptr || !texture->handle)
		{
			return;
		}

		// The commit step: the front-end handle is turned into the backend object the draw binds.
		// Descriptors that name a backend resource directly (render-target attachment, engine
		// fallback) have nothing to resolve.
		if (Rendering::HAL::Texture* resolved = Rendering::Backend::GetHandleAllocator().HandleCast<Rendering::HAL::Texture>(texture->handle))
		{
			texture->direct = resolved;
		}
	}

	bool Descriptor::IsValid() const
	{
		if (const BufferInfo* buffer = GetBuffer())
		{
			return static_cast<bool>(buffer->handle) && buffer->size > 0;
		}
		if (const TextureInfo* texture = GetTexture())
		{
			return texture->handle.operator bool() || texture->direct != nullptr;
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
			// The front end's handle when there is one; a backend-only texture (a render-target
			// attachment, the engine's 1x1 fallbacks) has no handle and only a GL name, which is
			// the side it lives on.
			const uint32_t handleId = texture->handle
				? texture->handle.GetId()
				: (texture->direct != nullptr ? texture->direct->GetHandleId() : 0);

			// A descriptor can still name a texture whose object was deleted (a resource reloaded
			// while an instance still points at the old one). Binding a stale name is a GL error
			// on every draw; skip it instead.
			if (viaCommand)
			{
				const uint32_t name = handleId == 0 && texture->direct != nullptr
					? texture->direct->GetID() : 0;
				if (handleId == 0 && name == 0)
				{
					return;
				}

				::Rendering::Backend::BindDescriptorRequest request;
				request.kind = ::Rendering::Backend::BindDescriptorRequest::Kind::Texture;
				request.handleId = handleId;
				request.objectId = name;
				request.slot = p_slot;
				driver->BindDescriptor(request);
				return;
			}

			if (texture->direct != nullptr && texture->direct->GetID() != 0
				&& glIsTexture(texture->direct->GetID()))
			{
				texture->direct->Bind(p_slot);
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
