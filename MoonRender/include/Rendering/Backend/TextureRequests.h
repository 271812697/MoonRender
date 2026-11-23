#pragma once
#include <cstdint>

#include <Maths/FVector4.h>

namespace Rendering::Backend
{
	/** A sampler parameter change on a texture that already exists.
	 *
	 * The border colour is what a sampled depth attachment needs (the grid's mirror plane samples
	 * depth outside the [0,1] range and needs it to read "in front of everything"), and it is GL
	 * state on the texture object - so with a render thread it is a command like the allocation is.
	 */
	struct SetTextureBorderColorRequest
	{
		uint32_t handleId = 0;
		Maths::FVector4 color{ 0.0f, 0.0f, 0.0f, 1.0f };
	};

	/** glGenerateTextureMipmap for a texture that already has storage. */
	struct GenerateTextureMipmapsRequest
	{
		uint32_t handleId = 0;
	};
}
