#include <Rendering/Backend/BackendResources.h>

#include <Rendering/Backend/DeferredDeleter.h>
#include <Rendering/HAL/ShaderProgram.h>
#include <Rendering/HAL/Buffer.h>
#include <Rendering/HAL/Framebuffer.h>
#include <Rendering/HAL/Renderbuffer.h>
#include <Rendering/HAL/VertexArray.h>

#include <glad/glad.h>

namespace Rendering::Backend::Records
{
	void CreateBuffer(ResourceHandle<HwBufferObject> p_handle, Rendering::Settings::EBufferType p_type)
	{
		HandleAllocator& allocator = GetHandleAllocator();

		// Once per handle, like CreateTexture: the recording side records a create whenever it
		// cannot see the object yet, and a second create would replace the object (and leak the GL
		// name the first one made).
		if (allocator.HandleCast<Rendering::HAL::Buffer>(p_handle) != nullptr)
		{
			return;
		}

		// The tagged constructor is the backend half: it makes the GL name, on the side that owns
		// the context, and derives the buffer's kind from what the create command carried (a
		// buffer's bind target comes from it, so it cannot be assumed - it used to be VERTEX for
		// every buffer, which is what the index and uniform buffers got wrong).
		allocator.Construct<Rendering::HAL::Buffer, HwBufferObject>(
			p_handle, p_type, Rendering::HAL::BackendConstructionTag{});
	}

	uint32_t BufferName(ResourceHandle<HwBufferObject> p_handle)
	{
		const Rendering::HAL::Buffer* const buffer =
			GetHandleAllocator().HandleCast<Rendering::HAL::Buffer>(p_handle);
		return buffer != nullptr ? buffer->GetID() : 0u;
	}

	uint32_t VertexArrayName(ResourceHandle<HwVertexArray> p_handle)
	{
		const Rendering::HAL::VertexArray* const array =
			GetHandleAllocator().HandleCast<Rendering::HAL::VertexArray>(p_handle);
		return array != nullptr ? array->GetID() : 0u;
	}

	uint32_t FramebufferName(ResourceHandle<HwFramebuffer> p_handle)
	{
		const Rendering::HAL::Framebuffer* const framebuffer =
			GetHandleAllocator().HandleCast<Rendering::HAL::Framebuffer>(p_handle);
		return framebuffer != nullptr ? framebuffer->GetID() : 0u;
	}

	uint32_t RenderbufferName(ResourceHandle<HwRenderbuffer> p_handle)
	{
		const Rendering::HAL::Renderbuffer* const renderbuffer =
			GetHandleAllocator().HandleCast<Rendering::HAL::Renderbuffer>(p_handle);
		return renderbuffer != nullptr ? renderbuffer->GetID() : 0u;
	}

	uint32_t ProgramName(ResourceHandle<HwProgram> p_handle)
	{
		const Rendering::HAL::ShaderProgram* const program =
			GetHandleAllocator().HandleCast<Rendering::HAL::ShaderProgram>(p_handle);
		return program != nullptr ? program->GetID() : 0u;
	}

	void CreateVertexArray(ResourceHandle<HwVertexArray> p_handle)
	{
		HandleAllocator& allocator = GetHandleAllocator();
		if (allocator.HandleCast<Rendering::HAL::VertexArray>(p_handle) != nullptr)
		{
			return;
		}
		// The object's constructor is what makes the GL name, the same pair as the buffer: the slot
		// here, the name where the context is (Filament's createVertexArrayR).
		allocator.Construct<Rendering::HAL::VertexArray, HwVertexArray>(
			p_handle, Rendering::HAL::BackendConstructionTag{});
	}

	void CreateFramebuffer(ResourceHandle<HwFramebuffer> p_handle)
	{
		HandleAllocator& allocator = GetHandleAllocator();
		if (allocator.HandleCast<Rendering::HAL::Framebuffer>(p_handle) != nullptr)
		{
			return;
		}
		allocator.Construct<Rendering::HAL::Framebuffer, HwFramebuffer>(
			p_handle, Rendering::HAL::BackendConstructionTag{});
	}

	void CreateRenderbuffer(ResourceHandle<HwRenderbuffer> p_handle)
	{
		HandleAllocator& allocator = GetHandleAllocator();
		if (allocator.HandleCast<Rendering::HAL::Renderbuffer>(p_handle) != nullptr)
		{
			return;
		}
		allocator.Construct<Rendering::HAL::Renderbuffer, HwRenderbuffer>(
			p_handle, Rendering::HAL::BackendConstructionTag{});
	}

	void CreateProgram(ResourceHandle<HwProgram> p_handle)
	{
		HandleAllocator& allocator = GetHandleAllocator();
		if (allocator.HandleCast<Rendering::HAL::ShaderProgram>(p_handle) != nullptr)
		{
			return;
		}
		// The constructor makes the GL program object; the executor then builds it in place
		// (compile, link, reflect) - Filament's createProgramR.
		allocator.Construct<Rendering::HAL::ShaderProgram, HwProgram>(
			p_handle, Rendering::HAL::BackendConstructionTag{});
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
		Rendering::HAL::Buffer* const buffer = allocator.HandleCast<Rendering::HAL::Buffer>(p_handle);
		if (buffer == nullptr)
		{
			return;
		}

		// The record is freed at once (its destructor touches no GL), and the GL half - the name
		// and the fence - is queued to the side that owns the context, like every other deletion
		// here.
		const uint32_t name = buffer->GetID();
		void* const fence = buffer->GetFence();

		allocator.Deallocate(p_handle);

		if (fence != nullptr)
		{
			DeferredDeleter::Push([fence] { glDeleteSync(static_cast<GLsync>(fence)); });
		}

		if (name != 0u)
		{
			// The deletion is GL too: it goes where the context is.
			DeferredDeleter::Push([name] { glDeleteBuffers(1, &name); });
		}
	}

	void DestroyVertexArray(ResourceHandle<HwVertexArray> p_handle)
	{
		HandleAllocator& allocator = GetHandleAllocator();
		Rendering::HAL::VertexArray* const array =
			allocator.HandleCast<Rendering::HAL::VertexArray>(p_handle);
		if (array == nullptr)
		{
			return;
		}

		const uint32_t name = array->GetID();
		allocator.Deallocate(p_handle);

		if (name != 0u)
		{
			DeferredDeleter::Push([name] { glDeleteVertexArrays(1, &name); });
		}
	}

	void DestroyFramebuffer(ResourceHandle<HwFramebuffer> p_handle)
	{
		HandleAllocator& allocator = GetHandleAllocator();
		Rendering::HAL::Framebuffer* const framebuffer =
			allocator.HandleCast<Rendering::HAL::Framebuffer>(p_handle);
		if (framebuffer == nullptr)
		{
			return;
		}

		const uint32_t name = framebuffer->GetID();
		allocator.Deallocate(p_handle);

		if (name != 0u)
		{
			DeferredDeleter::Push([name] { glDeleteFramebuffers(1, &name); });
		}
	}

	void DestroyRenderbuffer(ResourceHandle<HwRenderbuffer> p_handle)
	{
		HandleAllocator& allocator = GetHandleAllocator();
		Rendering::HAL::Renderbuffer* const renderbuffer =
			allocator.HandleCast<Rendering::HAL::Renderbuffer>(p_handle);
		if (renderbuffer == nullptr)
		{
			return;
		}

		const uint32_t name = renderbuffer->GetID();
		allocator.Deallocate(p_handle);

		if (name != 0u)
		{
			DeferredDeleter::Push([name] { glDeleteRenderbuffers(1, &name); });
		}
	}

	void DestroyProgram(ResourceHandle<HwProgram> p_handle)
	{
		HandleAllocator& allocator = GetHandleAllocator();
		Rendering::HAL::ShaderProgram* const program =
			allocator.HandleCast<Rendering::HAL::ShaderProgram>(p_handle);
		if (program == nullptr)
		{
			return;
		}

		const uint32_t name = program->GetID();
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
