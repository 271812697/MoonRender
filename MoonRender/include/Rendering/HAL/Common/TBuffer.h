#pragma once
#include <optional>
#include <Rendering/Settings/EGraphicsBackend.h>
#include <Rendering/Settings/EAccessSpecifier.h>
#include <Rendering/Settings/EBufferType.h>

namespace Rendering::HAL
{
	/**
	* Represents a range of memory in a buffer
	*/
	struct BufferMemoryRange
	{
		uint64_t offset;
		uint64_t size;
	};

	/**
	* Represents a buffer, used to store data on the GPU
	*/
	template<Settings::EGraphicsBackend Backend, class BufferContext>
	class TBuffer
	{
	public:
		/**
		* Creates a buffer
		* @param p_type
		*/
		TBuffer(Settings::EBufferType p_type);

		/**
		* Destroys the buffer
		*/
		~TBuffer();

		/**
		* Allocates memory for the buffer
		* @param p_size
		* @param p_usage
		* @return The size of the allocated memory in bytes
		*/
		uint64_t Allocate(uint64_t p_size, Settings::EAccessSpecifier p_usage = Settings::EAccessSpecifier::STATIC_DRAW);

		/**
		* Uploads data to the buffer
		* @param p_data
		* @param p_range
		*/
		void Upload(const void* p_data, std::optional<BufferMemoryRange> p_range = std::nullopt);

		/**
		* Returns true if the buffer is valid (properly allocated)
		*/
		bool IsValid() const;

		/**
		* Returns true if the buffer is empty
		*/
		bool IsEmpty() const;

		/**
		* Returns the size of the allocated buffer in bytes
		*/
		uint64_t GetSize() const;

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
		* Returns the ID of the buffer
		*/
		uint32_t GetID() const;

		/**
		* Maps a range of the buffer for reading on the CPU.
		* @param p_offset
		* @param p_size
		* @return Pointer to the mapped memory, or nullptr on failure
		*/
		void* MapRead(uint64_t p_offset, uint64_t p_size);

		/**
		* Unmaps the buffer previously mapped with MapRead()
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
