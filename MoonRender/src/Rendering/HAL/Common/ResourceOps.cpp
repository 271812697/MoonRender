#include <Rendering/HAL/Common/ResourceOps.h>

#include <Rendering/Backend/BufferRequests.h>
#include <Rendering/Backend/VertexArrayRequests.h>
#include <Rendering/HAL/VertexArray.h>
#include <Rendering/Context/Driver.h>
#include <Rendering/HAL/Common/HALDriverAccess.h>
#include <Rendering/HAL/OpenGL/GLBuffer.h>
#include <Rendering/HAL/OpenGL/GLTypes.h>

#include <glad/glad.h>

#include <cstdio>

#include <assert.h>
#include <memory>
#include <vector>

namespace Rendering::HAL::ResourceOps
{
	namespace
	{
		using BufferHandle = Rendering::Backend::ResourceHandle<Rendering::Backend::HwBufferObject>;

		Rendering::Context::Driver* CurrentDriver()
		{
			return ::Rendering::HAL::CurrentDriver();
		}

		/** Whether there is a driver to record into: then everything that needs the context is
		 * recorded, and the executor runs it - on another thread with a render thread, on this one
		 * when the frame is executed (docs/RenderThreadPlan.md §8.72). */
		bool IsRecorded(Rendering::Context::Driver* p_driver)
		{
			return p_driver != nullptr;
		}

		/** The backend object a handle names, or null when it does not exist (yet): the create
		 * command has not run, or the handle is stale/released. */
		Rendering::HAL::GLBuffer* Resolve(const BufferHandle& p_handle)
		{
			return Rendering::Backend::GetHandleAllocator().HandleCast<Rendering::HAL::GLBuffer>(p_handle);
		}

		uint32_t GetDataTypeSizeInBytes(Rendering::Settings::EDataType p_type)
		{
			switch (p_type)
			{
			case Rendering::Settings::EDataType::BYTE: return sizeof(GLbyte);
			case Rendering::Settings::EDataType::UNSIGNED_BYTE: return sizeof(GLubyte);
			case Rendering::Settings::EDataType::SHORT: return sizeof(GLshort);
			case Rendering::Settings::EDataType::UNSIGNED_SHORT: return sizeof(GLushort);
			case Rendering::Settings::EDataType::INT: return sizeof(GLint);
			case Rendering::Settings::EDataType::UNSIGNED_INT: return sizeof(GLuint);
			case Rendering::Settings::EDataType::FLOAT: return sizeof(GLfloat);
			case Rendering::Settings::EDataType::DOUBLE: return sizeof(GLdouble);
			default: return 0;
			}
		}
	}

	void EnsureBuffer(const BufferHandle& p_handle, Rendering::Settings::EBufferType p_type)
	{
		using namespace Rendering::Backend;

		if (!p_handle || Records::BufferName(p_handle) != 0)
		{
			return;
		}

		if (Rendering::Context::Driver* const driver = CurrentDriver())
		{
			// The name is made on the executing side, by the create command: the handle is usable
			// straight away for anything recorded after it (Filament's createBufferObjectS).
			driver->EnqueueCreateBuffer(CreateBufferRequest{ p_handle.GetId(), p_type });

			return;
		}

		Records::CreateBuffer(p_handle, p_type);
	}

	void EnsureVertexArray(const Rendering::Backend::ResourceHandle<Rendering::Backend::HwVertexArray>& p_handle)
	{
		using namespace Rendering::Backend;

		if (!p_handle || Records::VertexArrayName(p_handle) != 0)
		{
			return;
		}

		if (Rendering::Context::Driver* const driver = CurrentDriver())
		{
			driver->EnqueueCreateVertexArray(p_handle.GetId());
			return;
		}

		Records::CreateVertexArray(p_handle);
	}

	void AllocateBuffer(const BufferHandle& p_handle, uint64_t p_size, Rendering::Settings::EAccessSpecifier p_usage)
	{
		using namespace Rendering::Backend;

		if (!p_handle || p_size == 0)
		{
			return;
		}

		if (Rendering::Context::Driver* const driver = CurrentDriver(); IsRecorded(driver))
		{
			AllocateBufferRequest request;
			request.handleId = p_handle.GetId();
			request.size = p_size;
			request.usage = p_usage;
			driver->EnqueueAllocateBuffer(request);
			return;
		}

		if (Rendering::HAL::GLBuffer* const buffer = Resolve(p_handle))
		{
			buffer->Allocate(p_size, p_usage);
		}
	}

	void UploadBuffer(const BufferHandle& p_handle, const void* p_data, uint64_t p_offset, uint64_t p_size)
	{
		using namespace Rendering::Backend;

		if (!p_handle || p_data == nullptr || p_size == 0)
		{
			return;
		}

		if (Rendering::Context::Driver* const driver = CurrentDriver())
		{
			// Recorded, not executed: the handle and the bytes travel with the command
			// (Filament's BufferDescriptor) and the executor resolves the object it was given when
			// the buffer was created. Asking for the name here would be 0 - creation is a command
			// that has not run yet.
			driver->UpdateBufferRange(p_handle.GetId(), static_cast<uint32_t>(p_offset), p_data, static_cast<uint32_t>(p_size));
			return;
		}

		if (Rendering::HAL::GLBuffer* const buffer = Resolve(p_handle))
		{
			buffer->Upload(p_data, BufferMemoryRange{ p_offset, p_size });
		}
	}

	void BindBuffer(const BufferHandle& p_handle, Rendering::Settings::EBufferType p_type, std::optional<uint32_t> p_index)
	{
		using namespace Rendering::Backend;

		if (!p_handle)
		{
			return;
		}

		if (Rendering::Context::Driver* const driver = CurrentDriver())
		{
			// A binding point only means something on the side that owns the context: record it,
			// so the executor binds it before the draws that read it run. Binding it here left the
			// light SSBO unbound on the render thread and every lit surface read an empty light
			// list (docs §8.67).
			BindBufferRequest request;
			request.handleId = p_handle.GetId();
			request.type = p_type;
			request.indexed = p_index.has_value();
			request.index = p_index.value_or(0);
			driver->EnqueueBindBuffer(request);
			return;
		}

		if (Rendering::HAL::GLBuffer* const buffer = Resolve(p_handle))
		{
			buffer->Bind(p_index);
		}
	}

	void UnbindBuffer(Rendering::Settings::EBufferType p_type)
	{
		if (Rendering::Context::Driver* const driver = CurrentDriver())
		{
			Rendering::Backend::BindBufferRequest request;
			request.handleId = 0;
			request.type = p_type;
			driver->EnqueueBindBuffer(request);
			return;
		}

		glBindBuffer(EnumToValue<GLenum>(p_type), 0);
	}

	void BindBufferRange(const BufferHandle& p_handle, uint32_t p_index, uint64_t p_offset, uint64_t p_size)
	{
		using namespace Rendering::Backend;

		if (!p_handle)
		{
			return;
		}

		if (Rendering::Context::Driver* const driver = CurrentDriver())
		{
			BindBufferRangeRequest request;
			request.handleId = p_handle.GetId();
			request.index = p_index;
			request.offset = p_offset;
			request.size = p_size;
			driver->EnqueueBindBufferRange(request);
			return;
		}

		if (Rendering::HAL::GLBuffer* const buffer = Resolve(p_handle))
		{
			buffer->BindRange(p_index, p_offset, p_size);
		}
	}

	void UnmapBuffer(const BufferHandle& p_handle)
	{
		using namespace Rendering::Backend;

		if (!p_handle)
		{
			return;
		}

		if (Rendering::Context::Driver* const driver = CurrentDriver())
		{
			driver->EnqueueUnmapBuffer(p_handle.GetId());
			return;
		}

		if (Rendering::HAL::GLBuffer* const buffer = Resolve(p_handle))
		{
			buffer->Unmap();
		}
	}

	void InsertFence(const BufferHandle& p_handle)
	{
		using namespace Rendering::Backend;

		if (!p_handle)
		{
			return;
		}

		if (Rendering::Context::Driver* const driver = CurrentDriver())
		{
			driver->EnqueueInsertFence(p_handle.GetId());
			return;
		}

		if (Rendering::HAL::GLBuffer* const buffer = Resolve(p_handle))
		{
			buffer->InsertFence();
		}
	}

	void ClearFence(const BufferHandle& p_handle)
	{
		using namespace Rendering::Backend;

		if (!p_handle)
		{
			return;
		}

		if (Rendering::Context::Driver* const driver = CurrentDriver())
		{
			driver->EnqueueClearFence(p_handle.GetId());
			return;
		}

		if (Rendering::HAL::GLBuffer* const buffer = Resolve(p_handle))
		{
			buffer->ClearFence();
		}
	}

	bool IsFenceSignaled(const BufferHandle& p_handle)
	{
		Rendering::Context::Driver* const driver = CurrentDriver();
		if (driver == nullptr || driver->IsExecutorThread())
		{
			// No other side to ask, or already where the context is: run the query here.
			const Rendering::HAL::GLBuffer* const buffer = Resolve(p_handle);
			return buffer != nullptr ? buffer->IsFenceSignaled() : false;
		}

		// A synchronous driver call (Filament's getFenceStatus is a DECL_DRIVER_API_RETURN): the query
		// is recorded, and the answer comes back through the wait. Only for the calls that happen
		// between frames - the HZB readback uses DownloadBuffer instead, so a frame never waits.
		bool signaled = false;
		driver->Enqueue([&signaled, p_handle]
			{
				const Rendering::HAL::GLBuffer* const buffer = Resolve(p_handle);
				signaled = buffer != nullptr && buffer->IsFenceSignaled();
			});
		driver->FlushAndWait();
		return signaled;
	}

	void* MapRead(const BufferHandle& p_handle, uint64_t p_offset, uint64_t p_size)
	{
		Rendering::Context::Driver* const driver = CurrentDriver();
		if (driver == nullptr || driver->IsExecutorThread())
		{
			Rendering::HAL::GLBuffer* const buffer = Resolve(p_handle);
			return buffer != nullptr ? buffer->MapRead(p_offset, p_size) : nullptr;
		}

		// The same shape as IsFenceSignaled (Filament's mapBuffer is a DECL_DRIVER_API_RETURN): the
		// map runs where the context is and the pointer comes back. It stays mapped until the Unmap
		// command runs, so the caller reads it while the range is still mapped - exactly what
		// Filament's mapBuffer does. The caller has to be between frames, like IsFenceSignaled.
		void* mapped = nullptr;
		driver->Enqueue([&mapped, p_handle, p_offset, p_size]
			{
				Rendering::HAL::GLBuffer* const buffer = Resolve(p_handle);
				mapped = buffer != nullptr ? buffer->MapRead(p_offset, p_size) : nullptr;
			});
		driver->FlushAndWait();
		return mapped;
	}

	void SetVertexArrayLayout(
		const Rendering::Backend::ResourceHandle<Rendering::Backend::HwVertexArray>& p_array,
		const Rendering::Backend::ResourceHandle<Rendering::Backend::HwBufferObject>& p_vertexBuffer,
		const Rendering::Backend::ResourceHandle<Rendering::Backend::HwBufferObject>& p_indexBuffer,
		Rendering::Settings::VertexAttributeLayout p_attributes)
	{
		using namespace Rendering::Backend;

		if (!p_array || !p_vertexBuffer || p_attributes.empty())
		{
			return;
		}

		// The layout, in the shape the executor (and the backend object) reads it: one entry per
		// attribute with the stride and the offset already resolved.
		auto attributes = std::make_shared<std::vector<VertexLayoutAttribute>>();
		attributes->reserve(p_attributes.size());

		uint32_t stride = 0;
		for (const Rendering::Settings::VertexAttribute& attribute : p_attributes)
		{
			stride += GetDataTypeSizeInBytes(attribute.type) * attribute.count;
		}

		uint64_t offset = 0;
		for (const Rendering::Settings::VertexAttribute& attribute : p_attributes)
		{
			VertexLayoutAttribute entry;
			entry.type = attribute.type;
			entry.count = attribute.count;
			entry.normalized = attribute.normalized;
			entry.stride = stride;
			entry.offset = offset;
			attributes->push_back(entry);
			offset += GetDataTypeSizeInBytes(attribute.type) * attribute.count;
		}

		if (Rendering::Context::Driver* const driver = CurrentDriver())
		{
			SetVertexArrayLayoutRequest request;
			request.vertexArrayHandleId = p_array.GetId();
			request.vertexBufferHandleId = p_vertexBuffer.GetId();
			request.indexBufferHandleId = p_indexBuffer.GetId();
			request.attributes = std::move(attributes);
			driver->ApplyVertexArrayLayout(request);
			return;
		}

		// Inline: the array's own object applies the layout, exactly what the command's executor
		// does with it (there is no other side to run it on).
		Rendering::HAL::VertexArray* const array = Rendering::Backend::GetHandleAllocator()
			.HandleCast<Rendering::HAL::VertexArray>(p_array);
		const GLBuffer* const vertexBuffer = Resolve(p_vertexBuffer);
		const GLBuffer* const indexBuffer = Resolve(p_indexBuffer);
		if (array == nullptr || vertexBuffer == nullptr)
		{
			return;
		}
		array->SetLayout(*attributes, vertexBuffer->GetID(),
			indexBuffer != nullptr ? indexBuffer->GetID() : 0u);
	}

	void DestroyBuffer(const BufferHandle& p_handle)
	{
		Rendering::Backend::Records::DestroyBuffer(p_handle);
	}

	void DestroyVertexArray(const Rendering::Backend::ResourceHandle<Rendering::Backend::HwVertexArray>& p_handle)
	{
		Rendering::Backend::Records::DestroyVertexArray(p_handle);
	}
}
