#pragma once
#include <cstdint>
#include <functional>
#include <vector>
#include <memory>
#include <optional>

#include <Rendering/Backend/ResourceHandle.h>
#include <Rendering/HAL/BufferMemoryRange.h>
#include <Rendering/Settings/EAccessSpecifier.h>
#include <Rendering/Settings/EBufferType.h>

namespace Rendering::Backend
{
	struct HwBufferObject;
}

namespace Rendering::Data
{
	/** The front end of a buffer: what the recording side owns (a Mesh's vertex and index buffers,
	 * the engine's uniform blocks, the overlay's geometry).
	 *
	 * It holds the handle the driver gave it - the backend object, HAL::Buffer, lives in the handle's
	 * slot on the side that owns the context and is the only thing that has a GL name - plus what the
	 * recording side needs to size its operations (the kind of buffer and the size it allocated).
	 *
	 * Creation and every operation go through the driver (Rendering::Context::Driver::CreateBufferObject
	 * and friends), which is Filament's BufferObject calling Engine::getDriverApi(): the driver
	 * reserves the handle, records the create, and records each operation with the handle as its
	 * argument. Nothing here knows how a handle reaches the side that owns the context, and there is
	 * no "is there a driver" branch - that is the driver's contract, not the resource's.
	 *
	 * The object owns its handle: copying is disabled, moving transfers it.
	 */
	class BufferObject
	{
	public:
		/** An empty object: no handle, no type. Every operation is a no-op. */
		BufferObject() = default;

		/** Asks the driver for a handle and records the creation of its backend object. */
		explicit BufferObject(Settings::EBufferType p_type);

		/** Frees the record now and queues the GL deletion to the side that owns the context, so
		 * this is safe from any thread - including after the module (and its driver) is gone,
		 * which is where the long lived singletons release their buffers. */
		~BufferObject();

		BufferObject(const BufferObject&) = delete;
		BufferObject& operator=(const BufferObject&) = delete;
		BufferObject(BufferObject&& p_other) noexcept;
		BufferObject& operator=(BufferObject&& p_other) noexcept;

		/** Allocates storage, discarding what the buffer held. Returns the allocated size (0 when
		 * there was nothing to allocate). */
		uint64_t Allocate(
			uint64_t p_size,
			Settings::EAccessSpecifier p_usage = Settings::EAccessSpecifier::STATIC_DRAW);

		/** Uploads p_data into p_range, or into the whole buffer when no range is given. */
		void Upload(
			const void* p_data,
			std::optional<Rendering::HAL::BufferMemoryRange> p_range = std::nullopt);

		/** Binds the buffer, or one of its indexed binding points. */
		void Bind(std::optional<uint32_t> p_index = std::nullopt) const;

		/** Binds a byte range to an indexed binding point; what a block inside a shared buffer
		 * (a uniform-buffer pool slice) is addressed with. */
		void BindRange(uint32_t p_index, uint64_t p_offset, uint64_t p_size) const;

		/** Binds nothing to the buffer's target. */
		void Unbind() const;

		/** Copies a range into p_data once the GPU is done writing it (Filament's readPixels with a
		 * callback): the fence is checked where the context is, and p_onDone answers true when the
		 * bytes were copied, false when they are not ready yet - then ask again next frame. Nothing
		 * on this side waits for the GPU. */
		void Download(
			uint64_t p_offset,
			uint64_t p_size,
			std::shared_ptr<std::vector<uint8_t>> p_data,
			std::function<void(bool)> p_onDone) const;

		/** Unmaps a range that was mapped on the other side (a command: the unmap needs the
		 * context). */
		void Unmap();

		/** Records the fence that says the GPU finished the work submitted so far. */
		void InsertFence();

		/** Deletes the fence. */
		void ClearFence();

		uint64_t GetSize() const { return m_size; }
		Settings::EBufferType GetType() const { return m_type; }

		/** Whether this object names a backend object. */
		bool IsValid() const { return static_cast<bool>(m_handle); }

		const Rendering::Backend::ResourceHandle<Rendering::Backend::HwBufferObject>& GetHandle() const
		{
			return m_handle;
		}

	private:
		Rendering::Backend::ResourceHandle<Rendering::Backend::HwBufferObject> m_handle;
		Settings::EBufferType m_type = Settings::EBufferType::UNKNOWN;
		/** What the recording side knows about the storage: it sizes an upload or a range bind,
		 * because with a render thread the allocation is a command this side never sees run. */
		uint64_t m_size = 0;
	};
}
