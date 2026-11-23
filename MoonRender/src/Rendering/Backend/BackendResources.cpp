#include <Rendering/Backend/BackendResources.h>

#include <Rendering/Backend/DeferredDeleter.h>

#include <glad/glad.h>

namespace Rendering::Backend::Records
{
	void CreateBuffer(ResourceHandle<HwBufferObject> p_handle)
	{
		GLuint name = 0;
		glCreateBuffers(1, &name);
		GetHandleAllocator().Construct<HwBufferObject>(p_handle, name);
	}

	void CreateVertexArray(ResourceHandle<HwVertexArray> p_handle)
	{
		GLuint name = 0;
		glCreateVertexArrays(1, &name);
		GetHandleAllocator().Construct<HwVertexArray>(p_handle, name);
	}

	void CreateFramebuffer(ResourceHandle<HwFramebuffer> p_handle)
	{
		GLuint name = 0;
		glCreateFramebuffers(1, &name);
		GetHandleAllocator().Construct<HwFramebuffer>(p_handle, name);
	}

	void CreateRenderbuffer(ResourceHandle<HwRenderbuffer> p_handle)
	{
		GLuint name = 0;
		glCreateRenderbuffers(1, &name);
		GetHandleAllocator().Construct<HwRenderbuffer>(p_handle, name);
	}

	void RegisterProgram(ResourceHandle<HwProgram> p_handle, uint32_t p_name)
	{
		GetHandleAllocator().Construct<HwProgram>(p_handle, p_name);
	}

	Rendering::HAL::Texture* CreateTexture(
		ResourceHandle<Rendering::HAL::Texture> p_handle,
		Rendering::Settings::ETextureType p_type,
		std::string_view p_debugName)
	{
		HandleAllocator& allocator = GetHandleAllocator();

		// Created once per handle: callers keep the typed handle (and materials keep descriptors
		// naming it), so replacing the object would leave them pointing at a freed one.
		if (Rendering::HAL::Texture* const existing = allocator.HandleCast<Rendering::HAL::Texture>(p_handle))
		{
			return existing;
		}

		// The tagged constructor: this runs where the context is, and must not take the front
		// end's path (which would record another create command).
		return allocator.Construct<Rendering::HAL::Texture>(p_handle, p_type, p_debugName,
			Rendering::HAL::BackendConstructionTag{});
	}

	uint32_t TextureName(uint32_t p_handleId)
	{
		const ResourceHandle<Rendering::HAL::Texture> handle{ p_handleId };
		const Rendering::HAL::Texture* const object =
			GetHandleAllocator().HandleCast<Rendering::HAL::Texture>(handle);
		return object != nullptr ? object->GetID() : 0u;
	}

	void DestroyBuffer(ResourceHandle<HwBufferObject> p_handle)
	{
		HandleAllocator& allocator = GetHandleAllocator();
		const HwBufferObject* const record = allocator.HandleCast<HwBufferObject>(p_handle);
		if (record == nullptr)
		{
			return;
		}

		const uint32_t name = record->name;
		allocator.Deallocate(p_handle);

		if (name != 0u)
		{
			// The deletion is GL too: it goes where the context is.
			DeferredDeleter::Push([name] { glDeleteBuffers(1, &name); });
		}
	}

	void DestroyVertexArray(ResourceHandle<HwVertexArray> p_handle)
	{
		HandleAllocator& allocator = GetHandleAllocator();
		const HwVertexArray* const record = allocator.HandleCast<HwVertexArray>(p_handle);
		if (record == nullptr)
		{
			return;
		}

		const uint32_t name = record->name;
		allocator.Deallocate(p_handle);

		if (name != 0u)
		{
			DeferredDeleter::Push([name] { glDeleteVertexArrays(1, &name); });
		}
	}

	void DestroyFramebuffer(ResourceHandle<HwFramebuffer> p_handle)
	{
		HandleAllocator& allocator = GetHandleAllocator();
		const HwFramebuffer* const record = allocator.HandleCast<HwFramebuffer>(p_handle);
		if (record == nullptr)
		{
			return;
		}

		const uint32_t name = record->name;
		allocator.Deallocate(p_handle);

		if (name != 0u)
		{
			DeferredDeleter::Push([name] { glDeleteFramebuffers(1, &name); });
		}
	}

	void DestroyRenderbuffer(ResourceHandle<HwRenderbuffer> p_handle)
	{
		HandleAllocator& allocator = GetHandleAllocator();
		const HwRenderbuffer* const record = allocator.HandleCast<HwRenderbuffer>(p_handle);
		if (record == nullptr)
		{
			return;
		}

		const uint32_t name = record->name;
		allocator.Deallocate(p_handle);

		if (name != 0u)
		{
			DeferredDeleter::Push([name] { glDeleteRenderbuffers(1, &name); });
		}
	}

	void DestroyProgram(ResourceHandle<HwProgram> p_handle)
	{
		HandleAllocator& allocator = GetHandleAllocator();
		const HwProgram* const record = allocator.HandleCast<HwProgram>(p_handle);
		if (record == nullptr)
		{
			return;
		}

		const uint32_t name = record->name;
		allocator.Deallocate(p_handle);

		if (name != 0u)
		{
			DeferredDeleter::Push([name] { glDeleteProgram(name); });
		}
	}

	void DestroyTexture(ResourceHandle<Rendering::HAL::Texture> p_handle)
	{
		HandleAllocator& allocator = GetHandleAllocator();
		if (allocator.HandleCast<Rendering::HAL::Texture>(p_handle) == nullptr)
		{
			return;
		}

		// The object's destructor deletes the GL texture, so it runs at the next drain, with the
		// context current - which is also when the slot is freed. Until then the handle stays
		// reserved, so the slot cannot be handed to someone else and destroyed twice.
		DeferredDeleter::Push([p_handle] { GetHandleAllocator().Deallocate(p_handle); });
	}
}
