#include <algorithm>
#include <cstring>

#include <glad/glad.h>

#include <Rendering/Data/UniformBufferPool.h>

namespace Rendering::Data
{
	namespace
	{
		uint32_t RoundUp(uint32_t p_value, uint32_t p_alignment)
		{
			return (p_value + p_alignment - 1) / p_alignment * p_alignment;
		}
	}

	UniformBufferPool& UniformBufferPool::Get()
	{
		static UniformBufferPool* pool = new UniformBufferPool();
		return *pool;
	}

	uint32_t UniformBufferPool::Alignment()
	{
		static const uint32_t alignment = [] {
			GLint value = 256;
			glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &value);
			return static_cast<uint32_t>(value > 0 ? value : 256);
		}();
		return alignment;
	}

	UniformBufferPool::Slice UniformBufferPool::Acquire(uint32_t p_size)
	{
		if (p_size == 0)
		{
			return {};
		}

		const uint32_t alignment = Alignment();
		const uint32_t rounded = RoundUp(p_size, alignment);

		// First fit in the holes released by destroyed materials.
		for (auto it = m_free.begin(); it != m_free.end(); ++it)
		{
			if (it->second < rounded)
			{
				continue;
			}

			const uint64_t offset = it->first;
			const uint32_t remaining = it->second - rounded;
			m_free.erase(it);
			if (remaining > 0)
			{
				m_free.emplace(offset + rounded, remaining);
			}
			return { offset, p_size };
		}

		const uint64_t offset = (m_usedEnd + alignment - 1) / alignment * alignment;
		EnsureCapacity(offset + rounded);
		m_usedEnd = offset + rounded;
		return { offset, p_size };
	}

	void UniformBufferPool::Release(Slice p_slice)
	{
		if (!p_slice.IsValid())
		{
			return;
		}

		const uint32_t rounded = RoundUp(p_slice.size, Alignment());
		const auto inserted = m_free.emplace(p_slice.offset, rounded);
		if (!inserted.second)
		{
			return; // released twice: ignore rather than corrupt the free list
		}

		auto it = inserted.first;
		if (auto next = std::next(it); next != m_free.end() && it->first + it->second == next->first)
		{
			it->second += next->second;
			m_free.erase(next);
		}
		if (it != m_free.begin())
		{
			auto previous = std::prev(it);
			if (previous->first + previous->second == it->first)
			{
				previous->second += it->second;
				m_free.erase(it);
			}
		}
	}

	void UniformBufferPool::EnsureCapacity(uint64_t p_required)
	{
		if (p_required <= m_capacity)
		{
			return;
		}

		uint64_t capacity = m_capacity == 0 ? 64ULL * 1024 : m_capacity * 2;
		while (capacity < p_required)
		{
			capacity *= 2;
		}

		if (m_buffer == nullptr)
		{
			m_buffer = std::make_unique<Rendering::HAL::UniformBuffer>();
		}

		// Re-specifying the storage discards it: put the live region back from the CPU mirror.
		m_buffer->Allocate(capacity, Rendering::Settings::EAccessSpecifier::DYNAMIC_DRAW);
		if (m_usedEnd > 0)
		{
			m_buffer->Upload(m_data.data(), Rendering::HAL::BufferMemoryRange{ 0, m_usedEnd });
		}
		m_data.resize(capacity, 0);
		m_capacity = capacity;
	}

	void UniformBufferPool::Write(Slice p_slice, const void* p_data)
	{
		if (!p_slice.IsValid() || m_buffer == nullptr)
		{
			return;
		}
		if (p_slice.offset + p_slice.size > m_capacity)
		{
			return;
		}

		std::memcpy(m_data.data() + p_slice.offset, p_data, p_slice.size);
		m_buffer->Upload(p_data, Rendering::HAL::BufferMemoryRange{ p_slice.offset, p_slice.size });
	}

	void UniformBufferPool::Bind(Slice p_slice, uint32_t p_bindingPoint) const
	{
		if (!p_slice.IsValid() || m_buffer == nullptr)
		{
			return;
		}
		m_buffer->BindRange(p_bindingPoint, p_slice.offset, p_slice.size);
	}
}
