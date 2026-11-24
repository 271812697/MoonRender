#pragma once
#include <cstdint>
#include <Rendering/HAL/Common/TVertexArray.h>

namespace Rendering::HAL
{
	/** The GL state of the backend object: the array's name. The array state itself - the attribute
	 * pointers and the element buffer binding - is stored in the GL array object by
	 * glVertexAttribPointer while that array is bound. */
	struct GLVertexArrayContext
	{
		uint32_t glId = 0;
	};

	using GLVertexArray = TVertexArray<Settings::EGraphicsBackend::OPENGL, GLVertexArrayContext>;
}
