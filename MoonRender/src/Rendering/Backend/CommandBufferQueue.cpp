#include <Rendering/Backend/CommandBufferQueue.h>

#include <Rendering/Backend/CommandStream.h>

#include <algorithm>
#include <cassert>

namespace Rendering::Backend
{
	namespace
	{
		size_t RangeSize(const CircularBuffer::Range& p_batch)
		{
			return static_cast<size_t>(static_cast<char*>(p_batch.head) - static_cast<char*>(p_batch.tail));
		}

		size_t RoundUpToBlock(size_t p_size)
		{
			const size_t block = CircularBuffer::GetBlockSize();
			return (p_size + block - 1) & ~(block - 1);
		}
	}

	CommandBufferQueue::CommandBufferQueue(size_t p_requiredSize, size_t p_bufferSize)
		: m_circularBuffer(std::max(RoundUpToBlock(p_requiredSize), p_bufferSize)),
		m_requiredSize(RoundUpToBlock(p_requiredSize)),
		m_freeSpace(m_circularBuffer.Size())
	{
		assert(m_circularBuffer.Size() >= m_requiredSize);
	}

	CommandBufferQueue::~CommandBufferQueue() = default;

	void CommandBufferQueue::Flush()
	{
		CircularBuffer& ring = m_circularBuffer;
		if (ring.IsEmpty())
		{
			return;
		}

		// Terminates the batch: the executor stops there (its next is nullptr).
		new (ring.Allocate(CommandBase::Align(sizeof(NoopCommand)))) NoopCommand(nullptr);

		const CircularBuffer::Range batch = ring.GetBuffer();
		const size_t used = RangeSize(batch);

		std::unique_lock lock(m_lock);
		assert(used <= m_freeSpace);
		m_freeSpace -= used;
		m_batches.push_back(batch);
		m_condition.notify_one();

		if (m_freeSpace < m_requiredSize)
		{
			// The ring is full: wait until the consumer has executed and released a batch. On the
			// same thread this only happens when a frame recorded more than the whole ring, which
			// the sizes rule out - the assertion above would fire first.
			m_condition.wait(lock, [this] { return m_freeSpace >= m_requiredSize || m_exitRequested; });
		}
	}

	std::vector<CircularBuffer::Range> CommandBufferQueue::TakeCommands()
	{
		std::lock_guard lock(m_lock);
		std::vector<CircularBuffer::Range> batches;
		batches.swap(m_batches);
		return batches;
	}

	std::vector<CircularBuffer::Range> CommandBufferQueue::WaitForCommands()
	{
		std::unique_lock lock(m_lock);
		m_condition.wait(lock, [this] { return !m_batches.empty() || m_exitRequested; });
		std::vector<CircularBuffer::Range> batches;
		batches.swap(m_batches);
		return batches;
	}

	void CommandBufferQueue::ReleaseBuffer(const CircularBuffer::Range& p_batches)
	{
		size_t released = RangeSize(p_batches);
		std::lock_guard lock(m_lock);
		m_freeSpace += released;
		m_condition.notify_one();
	}

	void CommandBufferQueue::ReleaseBuffers(const std::vector<CircularBuffer::Range>& p_batches)
	{
		size_t released = 0;
		for (const CircularBuffer::Range& batch : p_batches)
		{
			released += RangeSize(batch);
		}

		std::lock_guard lock(m_lock);
		m_freeSpace += released;
		m_condition.notify_all();
	}

	void CommandBufferQueue::RequestExit()
	{
		std::lock_guard lock(m_lock);
		m_exitRequested = true;
		m_condition.notify_all();
	}

	size_t CommandBufferQueue::GetUsed() const
	{
		std::lock_guard lock(m_lock);
		return m_circularBuffer.Size() - m_freeSpace;
	}
}
