#pragma once
#include <Rendering/Backend/BackendResources.h>
#include <Rendering/Settings/EAccessSpecifier.h>
#include <Rendering/Settings/VertexAttribute.h>

#include <cstdint>
#include <optional>

namespace Rendering::HAL
{
	/** The backend operations a front-end object needs once it holds *handles* instead of HAL
	 * objects: make the GL object, give a buffer its storage, upload bytes into it, apply a
	 * vertex layout.
	 *
	 * A front-end object (a Mesh, with its VBO / IBO / VAO handles) cannot call GL: with a render
	 * thread the context belongs to the executor. So each operation has two halves, the same split
	 * Filament's `VertexBuffer::setBufferAt` -> `DriverApi.updateBufferObject` has:
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
	 */
	namespace ResourceOps
	{
		/** Creates the GL buffer behind the handle; a no-op when it already exists. */
		void EnsureBuffer(const Rendering::Backend::ResourceHandle<Rendering::Backend::HwBufferObject>& p_handle);

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
