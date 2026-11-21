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
			return buffer->bufferId != 0 && buffer->size > 0;
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
		// The bind is a command when the module runs its batches on a render thread (Filament's
		// bindDescriptorSet): the recording side has no context, so a direct glBindBufferRange here
		// simply does nothing and every draw then reads an unbound UBO - which is what an empty
		// scene looks like.
		::Rendering::Context::Driver* const driver = ::Rendering::HAL::CurrentDriver();
		const bool viaCommand = driver != nullptr && driver->IsRenderThreadActive();

		if (const BufferInfo* buffer = GetBuffer())
		{
			if (viaCommand)
			{
				::Rendering::Backend::BindDescriptorRequest request;
				request.kind = buffer->type == BufferType::ShaderStorage
					? ::Rendering::Backend::BindDescriptorRequest::Kind::ShaderStorageBuffer
					: ::Rendering::Backend::BindDescriptorRequest::Kind::UniformBuffer;
				request.objectId = buffer->bufferId;
				request.slot = p_slot;
				request.offset = buffer->offset;
				request.size = buffer->size;
				driver->BindDescriptor(request);
				return;
			}

			const GLenum target = buffer->type == BufferType::ShaderStorage
				? GL_SHADER_STORAGE_BUFFER : GL_UNIFORM_BUFFER;
			glBindBufferRange(target, p_slot, buffer->bufferId,
				buffer->offset, buffer->size);
			return;
		}

		if (const TextureInfo* texture = GetTexture())
		{
			// A descriptor can still name a texture whose object was deleted (a resource reloaded
			// while an instance still points at the old one). Binding a stale name is a GL error
			// on every draw; skip it instead.
			if (texture->direct != nullptr && texture->direct->GetID() != 0)
			{
				if (viaCommand)
				{
					::Rendering::Backend::BindDescriptorRequest request;
					request.kind = ::Rendering::Backend::BindDescriptorRequest::Kind::Texture;
					request.objectId = texture->direct->GetID();
					request.slot = p_slot;
					driver->BindDescriptor(request);
					return;
				}

				if (glIsTexture(texture->direct->GetID()))
				{
					texture->direct->Bind(p_slot);
				}
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
		// An upload is a command (Filament's updateBufferObject): the recording side only names the
		// range, the executor writes it. Direct GL here would run without a context on the frame's
		// thread as soon as the batches live on a render thread.
		::Rendering::Context::Driver* const driver = ::Rendering::HAL::CurrentDriver();
		if (driver != nullptr && driver->IsRenderThreadActive())
		{
			driver->UpdateBufferRange(buffer->bufferId, buffer->offset, p_data,
				std::min(p_size, buffer->size));
			return;
		}

		glNamedBufferSubData(buffer->bufferId, buffer->offset,
			std::min(p_size, buffer->size), p_data);
	}
}
