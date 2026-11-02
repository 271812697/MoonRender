#include <Rendering/Resources/ChunkContainer.h>

#include <Rendering/Resources/Unflattener.h>

namespace Rendering::Resources
{
	bool ChunkContainer::Parse()
	{
		m_chunks.clear();
		if (m_data == nullptr || m_size < sizeof(uint64_t) + sizeof(uint32_t))
		{
			return false;
		}

		Unflattener reader(m_data, m_size);
		while (reader.HasData())
		{
			uint64_t type = 0;
			uint32_t size = 0;
			if (!reader.Read(&type) || !reader.Read(&size))
			{
				return false;
			}

			const uint8_t* start = reader.GetCursor();
			if (!reader.Skip(size))
			{
				return false;
			}

			m_chunks.push_back(Chunk{ static_cast<Moon::Material::ChunkType>(type), start, size });
		}
		return !m_chunks.empty();
	}

	bool ChunkContainer::HasChunk(Moon::Material::ChunkType p_type) const
	{
		return GetChunk(p_type) != nullptr;
	}

	const ChunkContainer::Chunk* ChunkContainer::GetChunk(Moon::Material::ChunkType p_type) const
	{
		for (const Chunk& chunk : m_chunks)
		{
			if (chunk.type == p_type)
			{
				return &chunk;
			}
		}
		return nullptr;
	}
}
