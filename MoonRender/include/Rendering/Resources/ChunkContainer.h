#pragma once
#include <cstdint>
#include <vector>

#include <Rendering/Resources/MaterialChunkType.h>

namespace Rendering::Resources
{
	/** Walks a package's flat chunk list. This is filaflat's ChunkContainer, reduced to what
	 * the engine reads: a chunk is (uint64 type, uint32 size, payload) and chunks are stored in
	 * the order they appear. There is no package header; the loader recognises a package from
	 * its first chunk (MaterialVersion). */
	class ChunkContainer
	{
	public:
		struct Chunk
		{
			Moon::Material::ChunkType type = Moon::Material::ChunkType::Unknown;
			const uint8_t* start = nullptr;
			size_t size = 0;
		};

		ChunkContainer() = default;
		ChunkContainer(const void* p_data, size_t p_size)
			: m_data(static_cast<const uint8_t*>(p_data)), m_size(p_size) {}

		/** Parses the chunk list. False when a chunk header or its payload runs past the
		 * buffer; chunks parsed before the failure remain accessible. */
		bool Parse();

		bool HasChunk(Moon::Material::ChunkType p_type) const;
		const Chunk* GetChunk(Moon::Material::ChunkType p_type) const;

		const std::vector<Chunk>& Chunks() const { return m_chunks; }
		const uint8_t* Data() const { return m_data; }
		size_t DataSize() const { return m_size; }

	private:
		const uint8_t* m_data = nullptr;
		size_t m_size = 0;
		std::vector<Chunk> m_chunks;
	};
}
