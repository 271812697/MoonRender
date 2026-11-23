#pragma once
#include <cstdint>

#include <Rendering/Resources/IMesh.h>
#include <Rendering/Settings/EPrimitiveMode.h>

namespace Rendering::Backend
{
	/** One draw call, as a command.
	 *
	 * It carries the mesh, not a resolved vertex-array name: the array a mesh binds is queried
	 * when the command is replayed, on the render side. That is what lets a mesh be created and
	 * uploaded after the frame that recorded the draw was built - the deferred upload runs
	 * before the batch is executed (see Backend::DeferredDeleter), so the name is there by the
	 * time this asks for it, and no GL name ever travels through the recording side. */
	struct DrawMeshRequest
	{
		const ::Rendering::Resources::IMesh* mesh = nullptr;
		int subIndex = 0;
		::Rendering::Settings::EPrimitiveMode primitiveMode = ::Rendering::Settings::EPrimitiveMode::TRIANGLES;
		uint32_t instances = 1;
	};
}
