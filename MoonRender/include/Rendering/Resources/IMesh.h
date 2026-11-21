#pragma once
#include <Rendering/HAL/VertexArray.h>
#include <Rendering/Geometry/BoundingSphere.h>
#include <Rendering/Geometry/bbox.h>

namespace Rendering::Resources
{
	/**
	* Interface for any mesh
	*/
	class IMesh
	{
	public:
		virtual void Bind(int index=0) const = 0;
		virtual void Unbind() const = 0;
		virtual uint32_t GetVertexCount() const = 0;
		virtual uint32_t GetIndexCount(int index=0) const = 0;
		/** The backend name of the array object the draw path binds for sub-mesh p_index: the mesh
		 * holds the vertex array's handle (see Resources/Mesh.h) and this resolves it on the side
		 * that owns the context. */
		virtual uint32_t GetVertexArrayId(int p_index = 0) const { (void)p_index; return 0; }
		virtual const Rendering::Geometry::BoundingSphere& GetBoundingSphere()  = 0;
		virtual const Rendering::Geometry::bbox& GetBoundingBox()=0;
	};
}