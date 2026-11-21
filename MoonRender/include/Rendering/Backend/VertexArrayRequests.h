#pragma once
#include <cstdint>
#include <memory>
#include <vector>

#include <Rendering/Settings/EDataType.h>

namespace Rendering::Backend
{
	/** One attribute of a vertex layout, as the executor needs it. */
	struct VertexLayoutAttribute
	{
		::Rendering::Settings::EDataType type = ::Rendering::Settings::EDataType::FLOAT;
		uint8_t count = 4;
		bool normalized = false;
		uint32_t stride = 0;
		uint64_t offset = 0;
	};

	/** Applies a vertex layout to a vertex array on the side that owns the context.
	 *
	 * The recording side knows the layout (it is the mesh's, built when the mesh is created) but
	 * cannot touch GL: glVertexAttribPointer/glEnableVertexAttribArray need the context, and with a
	 * render thread that context belongs to the executor. The attributes travel on the heap, like
	 * the upload payloads, so the command does not depend on the caller's memory.
	 */
	struct SetVertexArrayLayoutRequest
	{
		uint32_t vertexArrayId = 0;
		uint32_t vertexBufferId = 0;
		uint32_t indexBufferId = 0;
		std::shared_ptr<const std::vector<VertexLayoutAttribute>> attributes;
	};
}
