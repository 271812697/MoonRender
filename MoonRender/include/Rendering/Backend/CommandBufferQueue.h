#pragma once
#include <condition_variable>
#include <mutex>
#include <vector>

#include <Rendering/Backend/CircularBuffer.h>

namespace Rendering::Backend
{
	/** The hand-off between the thread that records commands and the thread that executes them,
	 * with a CircularBuffer as the storage.
	 *
	 * Ported from Filament's CommandBufferQueue. The producer records into the ring and publishes
	 * a batch with Flush() (which appends the terminating NoopCommand and blocks while the ring is
	 * running full); the consumer takes batches with WaitForCommands() and gives the space back
	 * with ReleaseBuffers().
	 *
	 * Today both ends run on the main thread - Driver::ExecutePendingCommands() flushes and then
	 * executes - which is why TakeCommands() (no waiting) exists: waiting for a batch the same
	 * thread has just published would only deadlock. When the executor moves to its own thread the
	 * producer keeps calling Flush() and the executor calls WaitForCommands()/ReleaseBuffers(),
	 * and nothing else changes. */
	class CommandBufferQueue
	{
	public:
		/** p_requiredSize is the space a batch is guaranteed to have after Flush() (round it up
		 * to a block); p_bufferSize is the ring, at least twice p_requiredSize so the producer
		 * does not block on every frame. */
		CommandBufferQueue(size_t p_requiredSize, size_t p_bufferSize);
		~CommandBufferQueue();

		CommandBufferQueue(const CommandBufferQueue&) = delete;
		CommandBufferQueue& operator=(const CommandBufferQueue&) = delete;

		CircularBuffer& GetCircularBuffer() noexcept { return m_circularBuffer; }

		/** Producer: closes the batch being recorded and publishes it, then waits while the ring
		 * is full. Does nothing when nothing was recorded. */
		void Flush();

		/** Consumer, same thread: the batches published so far, without waiting. */
		std::vector<CircularBuffer::Range> TakeCommands();

		/** Consumer, other thread: blocks until there is a batch, or until exit was requested. */
		std::vector<CircularBuffer::Range> WaitForCommands();

		/** Gives the space of executed batches back to the producer and wakes it. */
		void ReleaseBuffer(const CircularBuffer::Range& p_batches);
		void ReleaseBuffers(const std::vector<CircularBuffer::Range>& p_batches);

		/** Makes a waiting consumer return (it sees an empty list) so its thread can end. */
		/** Blocks until every published batch has been released (executed): the render thread has
		 * run everything recorded so far. Filament's flushAndWait without the GPU fence, which is
		 * enough when the caller only needs the commands to have executed. */
		void WaitForIdle();

		void RequestExit();

		/** Bytes handed out and not released yet, for the debug overlay / assertions. */
		size_t GetUsed() const;

	private:
		CircularBuffer m_circularBuffer;
		mutable std::mutex m_lock;
		mutable std::condition_variable m_condition;
		std::vector<CircularBuffer::Range> m_batches;
		size_t m_requiredSize = 0;
		size_t m_freeSpace = 0;
		bool m_exitRequested = false;
	};
}
