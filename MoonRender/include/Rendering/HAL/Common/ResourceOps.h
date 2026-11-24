#pragma once
#include <Rendering/Backend/BackendResources.h>
#include <Rendering/Settings/EAccessSpecifier.h>
#include <Rendering/Settings/EBufferType.h>
#include <Rendering/Settings/VertexAttribute.h>

#include <cstdint>
#include <optional>

namespace Rendering::HAL
{
	/** The backend operations a front-end object needs once it holds *handles* instead of HAL
	 * objects: make the GL object, give a buffer its storage, upload bytes into it, bind it, apply
	 * a vertex layout.
	 *
	 * A front-end object (a Mesh, with its VBO / IBO / VAO handles; a Data::BufferObject) cannot
	 * call GL: the context belongs to the executor. So each operation has two halves, the same
	 * split Filament's `VertexBuffer::setBufferAt` -> `DriverApi.updateBufferObject` has:
	 *
	 *  - with a driver, the work is recorded as the command that does it, and the ring runs it on
	 *    the side that owns the context (recording nothing needs GL);
	 *  - without one there is no other side to run it on, and it is done inline, which is what the
	 *    code did before the commands existed.
	 *
	 * The create/allocate steps keep their historical timing: a buffer's storage allocation is only
	 * recorded when a render thread exists (otherwise it runs inline), while an upload is always
	 * recorded when there is a driver - the difference does not matter to a caller, it is what the
	 * two implementations always did.
	 *
	 * The exceptions are MapRead and IsFenceSignaled: they return a CPU pointer / a GPU answer and
	 * cannot be a command without a way to wait for the executor (Filament's mapBuffer and
	 * getFenceStatus are `DECL_DRIVER_API_RETURN`). They run inline, so they require the calling
	 * thread to own the context; see the TODO on their definitions.
	 */
	namespace ResourceOps
	{
		/** Creates the GL buffer behind the handle (as the given kind of buffer); a no-op when it
		 * already exists. The kind is fixed at creation: a buffer's bind target is derived from
		 * it. */
		void EnsureBuffer(
			const Rendering::Backend::ResourceHandle<Rendering::Backend::HwBufferObject>& p_handle,
			Rendering::Settings::EBufferType p_type);

		/** Creates the GL vertex array behind the handle; a no-op when it already exists. */
		void EnsureVertexArray(const Rendering::Backend::ResourceHandle<Rendering::Backend::HwVertexArray>& p_handle);

		/** Storage for the buffer (glNamedBufferData). */
		void AllocateBuffer(
			const Rendering::Backend::ResourceHandle<Rendering::Backend::HwBufferObject>& p_handle,
			uint64_t p_size,
			Rendering::Settings::EAccessSpecifier p_usage = Rendering::Settings::EAccessSpecifier::STATIC_DRAW);

		/** p_size bytes at p_offset in the buffer. The size is explicit because the record does not
		 * carry it: with a render thread the allocation is a command, so the recording side never
		 * learns it. */
		void UploadBuffer(
			const Rendering::Backend::ResourceHandle<Rendering::Backend::HwBufferObject>& p_handle,
			const void* p_data,
			uint64_t p_offset,
			uint64_t p_size);

		/** Binds the buffer to its target, or to one of its indexed binding points. */
		void BindBuffer(
			const Rendering::Backend::ResourceHandle<Rendering::Backend::HwBufferObject>& p_handle,
			Rendering::Settings::EBufferType p_type,
			std::optional<uint32_t> p_index = std::nullopt);

		/** Binds nothing to p_type's target (the inverse of a plain BindBuffer). */
		void UnbindBuffer(Rendering::Settings::EBufferType p_type);

		/** Binds a byte range to an indexed binding point (glBindBufferRange), which is how a
		 * block inside a shared buffer is addressed. */
		void BindBufferRange(
			const Rendering::Backend::ResourceHandle<Rendering::Backend::HwBufferObject>& p_handle,
			uint32_t p_index,
			uint64_t p_offset,
			uint64_t p_size);

		/** Unmaps the buffer (glUnmapNamedBuffer). A command too: the unmap needs the context, and
		 * it has to land after the uploads that wrote the mapped range. */
		void UnmapBuffer(const Rendering::Backend::ResourceHandle<Rendering::Backend::HwBufferObject>& p_handle);

		/** Records the fence that InsertFence() inserts, on the side that owns the context. */
		void InsertFence(const Rendering::Backend::ResourceHandle<Rendering::Backend::HwBufferObject>& p_handle);
		void ClearFence(const Rendering::Backend::ResourceHandle<Rendering::Backend::HwBufferObject>& p_handle);

		/** Non-blocking fence query. Synchronous: the answer is GPU state, so it can only be asked
		 * where the context is (see the TODO on the definition). */
		bool IsFenceSignaled(const Rendering::Backend::ResourceHandle<Rendering::Backend::HwBufferObject>& p_handle);

		/** Maps p_size bytes at p_offset for reading; null on failure. Synchronous, like
		 * IsFenceSignaled: the pointer only means something where the context is. */
		void* MapRead(
			const Rendering::Backend::ResourceHandle<Rendering::Backend::HwBufferObject>& p_handle,
			uint64_t p_offset,
			uint64_t p_size);

		/** Applies the layout to the array and binds the two buffers into it (what Filament does
		 * when a vertex buffer is described to a primitive). */
		void SetVertexArrayLayout(
			const Rendering::Backend::ResourceHandle<Rendering::Backend::HwVertexArray>& p_array,
			const Rendering::Backend::ResourceHandle<Rendering::Backend::HwBufferObject>& p_vertexBuffer,
			const Rendering::Backend::ResourceHandle<Rendering::Backend::HwBufferObject>& p_indexBuffer,
			Rendering::Settings::VertexAttributeLayout p_attributes);

		/** Frees the record and queues the GL deletion to the side that owns the context. */
		void DestroyBuffer(const Rendering::Backend::ResourceHandle<Rendering::Backend::HwBufferObject>& p_handle);
		void DestroyVertexArray(const Rendering::Backend::ResourceHandle<Rendering::Backend::HwVertexArray>& p_handle);
	}
}
