#include "ChunkContainer.h"

#include "Flattener.h"

namespace Moon::Matc
{
	void ChunkContainer::Flatten(Flattener& p_flattener) const
	{
		for (const auto& chunk : m_children)
		{
			p_flattener.WriteUint64(static_cast<uint64_t>(chunk->GetType()));
			const size_t sizePosition = p_flattener.BeginSize();
			chunk->Flatten(p_flattener);
			p_flattener.EndSize(sizePosition);
		}
	}
}
