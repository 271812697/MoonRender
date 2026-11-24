#pragma once
#include <Rendering/Backend/HandleAllocator.h>
#include <Rendering/HAL/Texture.h>
#include <Rendering/Settings/EBufferType.h>
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
	 * methods are the backend operations), and so is a buffer's: it is HAL::Buffer (TBuffer), the
	 * object the create-buffer command constructs in the slot. The front end of a buffer is
	 * Rendering::Data::BufferObject, which only holds the handle. The remaining records below are
	 * still name records for now, because the HAL object of a framebuffer/... is still the
	 * front-end object that also caches the name.
	 *
	 * HwResource is what those records have in common: a virtual destructor, so a slot can hold
	 * any of them and free it through the destructor stored next to it. */
	struct HwResource
	{
		virtual ~HwResource() = default;
	};

	/** A buffer object (Filament's HwBufferObject): the *backend* base of every buffer kind, with
	 * the kind it was created as and the size its storage was allocated with - the state anything
	 * holding only the handle can read. The OpenGL object itself (the GL name and the fence) is
	 * HAL::Buffer, which derives from this and lives in the same slot. */
	struct HwBufferObject : HwResource
	{
		explicit HwBufferObject(Settings::EBufferType p_type = Settings::EBufferType::UNKNOWN) noexcept
			: type(p_type) {}

		Settings::EBufferType type = Settings::EBufferType::UNKNOWN;
		uint64_t size = 0;
	};

	/** A vertex array (Filament's HwVertexArray). The object in the slot is Backend::GLVertexArray,
	 * which owns the GL name and every operation; nothing about it is shared state. */
	struct HwVertexArray : HwResource {};

	/** A render target (Filament's HwFramebuffer); the object in the slot is
	 * Backend::GLFramebuffer. */
	struct HwFramebuffer : HwResource {};

	/** The depth/stencil storage of a render target (Filament's HwRenderbuffer); the object in the
	 * slot is Backend::GLRenderbuffer. */
	struct HwRenderbuffer : HwResource {};

	/** A shader program (Filament's HwProgram). The object in the slot is Backend::GLProgram, which
	 * owns the GL name and the build. The reflection the build found stays here, on the record: it
	 * is the answer to glGetActiveUniform / glGetUniformLocation - a GL query - and what a front
	 * end reads back once the create command has run. */
	struct HwProgram : HwResource
	{
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
		/** Constructs the HAL buffer in its slot - the constructor is what creates the GL name -
		 * once per handle. A buffer's kind is fixed at creation, because a bind target is derived
		 * from it (a uniform buffer, an index buffer, ...). */
		void CreateBuffer(ResourceHandle<HwBufferObject> p_handle, Rendering::Settings::EBufferType p_type);
		void CreateVertexArray(ResourceHandle<HwVertexArray> p_handle);
		void CreateFramebuffer(ResourceHandle<HwFramebuffer> p_handle);
		void CreateRenderbuffer(ResourceHandle<HwRenderbuffer> p_handle);

		/** Constructs the HAL texture in its slot (the constructor is what creates the GL name),
		 * once per handle. Returns the object, or null for a stale handle. */
		Rendering::HAL::Texture* CreateTexture(
			ResourceHandle<Rendering::HAL::Texture> p_handle,
			Rendering::Settings::ETextureType p_type,
			std::string_view p_debugName);

		/** Constructs the program in its slot (the constructor makes the GL program object). The
		 * build - compile, link, reflect - is the executor's next step on the same object. */
		void CreateProgram(ResourceHandle<HwProgram> p_handle);

		void DestroyBuffer(ResourceHandle<HwBufferObject> p_handle);
		void DestroyVertexArray(ResourceHandle<HwVertexArray> p_handle);
		void DestroyFramebuffer(ResourceHandle<HwFramebuffer> p_handle);
		void DestroyRenderbuffer(ResourceHandle<HwRenderbuffer> p_handle);
		void DestroyProgram(ResourceHandle<HwProgram> p_handle);
		void DestroyTexture(ResourceHandle<Rendering::HAL::Texture> p_handle);

		/** handle_cast + the GL name, i.e. Filament's `handle_cast<GLBufferObject*>(h)->gl.id`.
		 * 0 while the object does not exist (yet), which is what the callers of the old name
		 * tables did with the same meaning. Defined in the .cpp: the record is HAL::Buffer, which
		 * cannot be named here (it includes this header). */
		uint32_t BufferName(ResourceHandle<HwBufferObject> p_handle);

		/** The same for the other backend objects: handle_cast to the OpenGL object and its name.
		 * 0 while the object does not exist (yet), or when the handle is stale. */
		uint32_t VertexArrayName(ResourceHandle<HwVertexArray> p_handle);
		uint32_t FramebufferName(ResourceHandle<HwFramebuffer> p_handle);
		uint32_t RenderbufferName(ResourceHandle<HwRenderbuffer> p_handle);
		uint32_t ProgramName(ResourceHandle<HwProgram> p_handle);

		/** The same for a texture, named by the plain id a front-end object carries (the HAL
		 * header cannot include this one - see GLTextureHandle.h). */
		uint32_t TextureName(uint32_t p_handleId);
	}
}
