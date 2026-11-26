#pragma once
#include <optional>

#include <Rendering/Backend/BackendResources.h>
#include <Rendering/HAL/BufferMemoryRange.h>
#include <Rendering/HAL/Common/BackendConstructionTag.h>
#include <Rendering/Settings/EAccessSpecifier.h>
#include <Rendering/Settings/EBufferType.h>
#include <Rendering/Settings/EGraphicsBackend.h>

namespace Rendering::HAL
{
	/**
	* Represents a buffer, used to store data on the GPU.
	*
	* This is the *backend* object, not the front end: it is constructed in the slot a handle names
	* (see Backend::HandleAllocator and Backend::Records::CreateBuffer), it owns the GL name, and
	* its methods are the GL operations the executor runs (Filament's GLBufferObject, and the
	* methods of its driver). What the recording side holds instead is a handle, wrapped by
	* Rendering::Data::BufferObject, which only records commands.
	*
	* It derives from Backend::HwBufferObject, which is where the kind of buffer and the size of
	* its storage live: what anything holding just the handle can read.
	*/
	template<Settings::EGraphicsBackend Backend, class BufferContext>
	class TBuffer : public Rendering::Backend::HwBufferObject
	{
	public:
		/**
		* Creates the backend object: the GL name is made here, so this runs where the context is
		* (the executor of a create-buffer command). The recording side never runs it.
		* @param p_type
		*/
		TBuffer(Settings::EBufferType p_type, BackendConstructionTag);

		/**
		* Destroys the buffer. Does not touch GL: the name and the fence are deleted by
		* Backend::Records::DestroyBuffer, which queues them to the side that owns the context, so
		* this destructor can run on any thread (it runs wherever the slot is freed).
		*/
		~TBuffer() = default;

		/**
		* Allocates storage for the buffer (glNamedBufferData).
		* @param p_size
		* @param p_usage
		* @return The size of the allocated memory in bytes
		*/
		uint64_t Allocate(uint64_t p_size, Settings::EAccessSpecifier p_usage = Settings::EAccessSpecifier::STATIC_DRAW);

		/**
		* Uploads data to the buffer (glNamedBufferSubData).
		* @param p_data
		* @param p_range
		*/
		void Upload(const void* p_data, std::optional<BufferMemoryRange> p_range = std::nullopt);

		/**
		* Returns true if the buffer has its GL name (i.e. it is the backend object, alive).
		*/
		bool IsValid() const;

		/**
		* Returns true if the buffer is empty
		*/
		bool IsEmpty() const { return size == 0; }

		/**
		* Returns the size of the allocated buffer in bytes
		*/
		uint64_t GetSize() const { return size; }

		/**
		* Binds the buffer
		* @param p_index (Optional) Index to bind the buffer to
		*/
		void Bind(std::optional<uint32_t> p_index = std::nullopt) const;

		/**
		* Binds a byte range of the buffer to an indexed target (glBindBufferRange).
		* @param p_index Binding point
		* @param p_offset Offset of the range in the buffer
		* @param p_size Size of the range
		*/
		void BindRange(uint32_t p_index, uint64_t p_offset, uint64_t p_size) const;

		/**
		* Unbinds the buffer
		*/
		void Unbind() const;

		/**
		* Returns the GL name of the buffer
		*/
		uint32_t GetID() const;

		/** The GLsync of the most recent fence, or null when none is active. Read by
		 * Backend::Records::DestroyBuffer, which queues the fence's deletion to the side that owns
		 * the context. */
		void* GetFence() const;


		/**
		* Unmaps a range that was mapped on the side that owns the context. The recording side never
		* maps: a readback is Backend::Records' DownloadBufferObject, which copies once the fence says
		* the GPU is done.
		*/
		void Unmap();

		/**
		* Inserts a fence after the commands already submitted for this buffer
		@sa IsFenceSignaled()
		*/
		void InsertFence();

		/**
		* Non blocking check of the fence inserted by InsertFence()
		* @return True when the GPU finished the work submitted before the fence
		*/
		bool IsFenceSignaled() const;

		/**
		* Deletes the fence inserted by InsertFence()
		*/
		void ClearFence();

	protected:
		BufferContext m_buffer;
	};
}
