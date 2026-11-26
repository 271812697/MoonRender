#pragma once
#include <cstdint>
#include <memory>
#include <vector>

namespace Rendering::Backend
{

	/** Binds one descriptor of a set: a buffer range to an indexed slot, or a texture to a unit.
	 *
	 * This is the "descriptor set" half of Filament's model: the front end only decides which
	 * resource a binding names, the bind itself is a command, and the draw that follows it sees
	 * the binding because the executor applied it in order.
	 */
	struct BindDescriptorRequest
	{
		enum class Kind : uint8_t
		{
			UniformBuffer,
			ShaderStorageBuffer,
			Texture,
		};

		Kind kind = Kind::UniformBuffer;
		/** The front end's handle: a buffer for the buffer kinds, a texture for Texture. The
		 * executor resolves it to the object it binds. */
		uint32_t handleId = 0;
		/** Backend-only resources (render-target attachments, the engine's 1x1 fallbacks) have a GL
		 * name but no front-end handle; this is that name. */
		uint32_t objectId = 0;
		/** The indexed slot (buffer) or texture unit (texture), as the program's binding map says. */
		uint32_t slot = 0;
		uint32_t offset = 0;
		uint32_t size = 0;
	};
}
