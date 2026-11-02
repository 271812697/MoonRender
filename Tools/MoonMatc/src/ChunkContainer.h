#pragma once
#include <memory>
#include <utility>
#include <vector>

#include <Rendering/Resources/MaterialChunkType.h>

namespace Moon::Matc
{
	class Flattener;

	/** One node of the package chunk tree (filament's eiff::Chunk). A subclass owns the data
	 * for one chunk type and knows how to flatten it; CompileSource only decides which chunks
	 * to push and in what order. */
	class Chunk
	{
	public:
		virtual ~Chunk() = default;

		Moon::Material::ChunkType GetType() const noexcept { return m_type; }

		virtual void Flatten(Flattener& p_flattener) const = 0;

	protected:
		explicit Chunk(Moon::Material::ChunkType p_type) : m_type(p_type) {}

	private:
		Moon::Material::ChunkType m_type;
	};

	/** Filament's eiff::ChunkContainer: an ordered tree of chunks, flattened depth first. */
	class ChunkContainer
	{
	public:
		template<typename T, typename... Args>
		const T& Push(Args&&... p_args)
		{
			auto chunk = std::make_unique<T>(std::forward<Args>(p_args)...);
			T* result = chunk.get();
			m_children.push_back(std::move(chunk));
			return *result;
		}

		/** uint64 type, uint32 payload size, payload - one entry per chunk. */
		void Flatten(Flattener& p_flattener) const;

	private:
		std::vector<std::unique_ptr<Chunk>> m_children;
	};
}
