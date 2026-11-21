#pragma once
#include <Rendering/Backend/HandleAllocator.h>
#include <Rendering/HAL/Texture.h>
#include <Rendering/Settings/UniformInfo.h>

#include <string>
#include <unordered_map>

#include <cstdint>
#include <string_view>

namespace Rendering::Backend
{
	/** The backend objects a handle can name, in the shape of Filament's Hw* structs
	 * (filament/backend/include/backend/BackendUtils.h: HwBufferObject, HwTexture, HwProgram...).
	 *
	 * A record is *the* place the GL name lives: it is constructed in the handle's slot by the
	 * command that creates the object, so the name only ever exists on the side that owns the
	 * context, and the front end refers to the object by handle (Filament's HwTexture/HwProgram
	 * hold `gl.id` the same way).
	 *
	 * A texture's record is HAL::Texture itself (its constructor creates the GL name and its
	 * methods are the backend operations); the rest are name records for now, because the HAL
	 * object of a buffer/framebuffer/... is still the front-end object that also caches the name.
	 */
	struct HwBufferObject
	{
		explicit HwBufferObject(uint32_t p_name = 0) noexcept : name(p_name) {}
		uint32_t name = 0;
	};

	struct HwVertexArray
	{
		explicit HwVertexArray(uint32_t p_name = 0) noexcept : name(p_name) {}
		uint32_t name = 0;
	};

	struct HwFramebuffer
	{
		explicit HwFramebuffer(uint32_t p_name = 0) noexcept : name(p_name) {}
		uint32_t name = 0;
	};

	struct HwRenderbuffer
	{
		explicit HwRenderbuffer(uint32_t p_name = 0) noexcept : name(p_name) {}
		uint32_t name = 0;
	};

	struct HwProgram
	{
		explicit HwProgram(uint32_t p_name = 0) noexcept : name(p_name) {}
		uint32_t name = 0;
		/** What the program's reflection found (glGetActiveUniform / glGetUniformLocation), filled
		 * by the create-program command. The recording side reads it after the drain: the queries
		 * need the program and the context, so they belong here (Filament keeps the same data on
		 * the backend program). */
		std::unordered_map<std::string, Rendering::Settings::UniformInfo> uniforms;
		std::unordered_map<std::string, uint32_t> uniformLocations;
	};

	/** The create/destroy half of the backend records: the only place these GL objects are made
	 * and deleted, mirroring Filament's createXxxR / the driver's destroy.
	 *
	 * The create functions run where the context is - they are the execute side of the Create*
	 * commands, and are called directly when there is no driver to record into. The destroy
	 * functions may be called from any thread: the record is freed at once and the GL name is
	 * queued to the side that owns the context (DeferredDeleter), like Filament queues destruction
	 * to the driver thread. */
	namespace Records
	{
		void CreateBuffer(ResourceHandle<HwBufferObject> p_handle);
		void CreateVertexArray(ResourceHandle<HwVertexArray> p_handle);
		void CreateFramebuffer(ResourceHandle<HwFramebuffer> p_handle);
		void CreateRenderbuffer(ResourceHandle<HwRenderbuffer> p_handle);

		/** Registers a program the executor has already built and linked. */
		void RegisterProgram(ResourceHandle<HwProgram> p_handle, uint32_t p_name);

		/** Constructs the HAL texture in its slot (the constructor is what creates the GL name),
		 * once per handle. Returns the object, or null for a stale handle. */
		Rendering::HAL::Texture* CreateTexture(
			ResourceHandle<Rendering::HAL::Texture> p_handle,
			Rendering::Settings::ETextureType p_type,
			std::string_view p_debugName);

		void DestroyBuffer(ResourceHandle<HwBufferObject> p_handle);
		void DestroyVertexArray(ResourceHandle<HwVertexArray> p_handle);
		void DestroyFramebuffer(ResourceHandle<HwFramebuffer> p_handle);
		void DestroyRenderbuffer(ResourceHandle<HwRenderbuffer> p_handle);
		void DestroyProgram(ResourceHandle<HwProgram> p_handle);
		void DestroyTexture(ResourceHandle<Rendering::HAL::Texture> p_handle);

		/** handle_cast + the name field, i.e. Filament's `handle_cast<GLTexture*>(h)->gl.id`.
		 * 0 while the object does not exist (yet), which is what the callers of the old name
		 * tables did with the same meaning. */
		inline uint32_t BufferName(ResourceHandle<HwBufferObject> p_handle)
		{
			const HwBufferObject* const record = GetHandleAllocator().HandleCast<HwBufferObject>(p_handle);
			return record != nullptr ? record->name : 0u;
		}

		inline uint32_t VertexArrayName(ResourceHandle<HwVertexArray> p_handle)
		{
			const HwVertexArray* const record = GetHandleAllocator().HandleCast<HwVertexArray>(p_handle);
			return record != nullptr ? record->name : 0u;
		}

		inline uint32_t FramebufferName(ResourceHandle<HwFramebuffer> p_handle)
		{
			const HwFramebuffer* const record = GetHandleAllocator().HandleCast<HwFramebuffer>(p_handle);
			return record != nullptr ? record->name : 0u;
		}

		inline uint32_t RenderbufferName(ResourceHandle<HwRenderbuffer> p_handle)
		{
			const HwRenderbuffer* const record = GetHandleAllocator().HandleCast<HwRenderbuffer>(p_handle);
			return record != nullptr ? record->name : 0u;
		}

		inline uint32_t ProgramName(ResourceHandle<HwProgram> p_handle)
		{
			const HwProgram* const record = GetHandleAllocator().HandleCast<HwProgram>(p_handle);
			return record != nullptr ? record->name : 0u;
		}

		/** The same for a texture, named by the plain id a front-end object carries (the HAL
		 * header cannot include this one - see GLTextureHandle.h). */
		uint32_t TextureName(uint32_t p_handleId);
	}
}
