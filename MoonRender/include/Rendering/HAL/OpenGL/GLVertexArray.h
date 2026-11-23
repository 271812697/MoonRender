#pragma once
#include <Rendering/Backend/ResourceHandle.h>
#include <Rendering/HAL/Common/TVertexArray.h>
#include <Rendering/HAL/OpenGL/GLIndexBuffer.h>
#include <Rendering/HAL/OpenGL/GLVertexBuffer.h>
#include <Tools/Utils/OptRef.h>

namespace Rendering::Backend
{
	struct HwVertexArray;
}

namespace Rendering::HAL
{
	struct GLVertexArrayContext
	{
		/** A handle; the GL name is in the record it names (Backend::HwVertexArray), which only
		 * exists on the side that owns the context. */
		Rendering::Backend::ResourceHandle<Rendering::Backend::HwVertexArray> handle;
		uint32_t attributeCount = 0;
	};

	using GLVertexArray = TVertexArray<
		Settings::EGraphicsBackend::OPENGL,
		GLVertexArrayContext,
		GLVertexBufferContext,
		GLIndexBufferContext,
		GLBufferContext
	>;
}
