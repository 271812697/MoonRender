#pragma once
#include <cstddef>

namespace Rendering::Backend
{
	/** The ring of raw memory one batch of commands is recorded into.
	 *
	 * Ported from Filament's backend::CircularBuffer. The allocation is twice the nominal
	 * capacity, so a command that starts near the end of the ring is written in one piece - it
	 * runs into the second half - and the writer is re-based at the next batch boundary
	 * (GetBuffer). Filament aliases the two halves with mmap so the ring is "hard"; Windows has
	 * no such mapping and none is needed here: the used span is asserted to stay within the
	 * capacity (see Allocate), so the second half is only ever transient scratch.
	 *
	 * Not thread safe by itself: the producer records, the consumer executes, and the two hand
	 * a batch over through CommandBufferQueue.
	 */
	class CircularBuffer
	{
	public:
		/** One contiguous batch, as handed to the executor. */
		struct Range
		{
			void* tail = nullptr;
			void* head = nullptr;
		};

		explicit CircularBuffer(size_t p_capacity);
		~CircularBuffer();

		CircularBuffer(const CircularBuffer&) = delete;
		CircularBuffer& operator=(const CircularBuffer&) = delete;

		/** The smallest unit the queue keeps free, so a batch never has to be split. */
		static size_t GetBlockSize();

		/** Nominal capacity: how much may be used before a flush has to happen. */
		size_t Size() const { return m_capacity; }

		/** True when nothing was recorded since the last GetBuffer(). */
		bool IsEmpty() const { return m_tail == m_head; }

		/** Bytes recorded since the last GetBuffer(). */
		size_t GetUsed() const;

		/** Reserves p_size bytes, aligned for any type (see CommandBase::Align). The caller is
		 * responsible for not exceeding the capacity - the command stream flushes first. */
		void* Allocate(size_t p_size);

		/** Hands the recorded batch to the consumer and re-bases the writer for the next one. */
		Range GetBuffer();

	private:
		char* m_data = nullptr;
		size_t m_capacity = 0;
		void* m_tail = nullptr;	// first byte of the batch being recorded
		void* m_head = nullptr;	// next free byte
	};
}
