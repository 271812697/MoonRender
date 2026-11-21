#include <Rendering/Backend/CircularBuffer.h>

#include <cassert>
#include <cstdlib>

namespace Rendering::Backend
{
	size_t CircularBuffer::GetBlockSize()
	{
		// One page: whatever a single batch needs has to fit in a whole number of these, so a
		// batch can be published without being split across the wrap.
		return 4096;
	}

	CircularBuffer::CircularBuffer(size_t p_capacity) : m_capacity(p_capacity)
	{
		m_data = static_cast<char*>(std::malloc(2 * m_capacity + GetBlockSize()));
		assert(m_data != nullptr);
		m_tail = m_data;
		m_head = m_data;
	}

	CircularBuffer::~CircularBuffer()
	{
		std::free(m_data);
	}

	size_t CircularBuffer::GetUsed() const
	{
		return static_cast<size_t>(static_cast<char*>(m_head) - static_cast<char*>(m_tail));
	}

	void* CircularBuffer::Allocate(size_t p_size)
	{
		// The used span never exceeds the capacity, so the writer can always run into the second
		// half of the allocation instead of wrapping in the middle of a command.
		assert(GetUsed() + p_size <= m_capacity);
		char* const current = static_cast<char*>(m_head);
		m_head = current + p_size;
		return current;
	}

	CircularBuffer::Range CircularBuffer::GetBuffer()
	{
		const Range batch{ m_tail, m_head };
		m_tail = m_head;

		char* const end = m_data + m_capacity;
		if (static_cast<char*>(m_head) >= end)
		{
			// The batch ran past the nominal end: the writer starts again at the beginning.
			const size_t overflow = static_cast<size_t>(static_cast<char*>(m_head) - end);
			m_head = m_data + overflow;
			m_tail = m_head;
		}
		return batch;
	}
}
