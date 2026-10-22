#pragma once
#include <cstdint>

namespace Rendering::Geometry
{
	/** Fills the tangent attribute of a vertex buffer from its positions, normals and UVs
	 * (Lengyel's method, http://www.terathon.com/code/tangent.html).
	 *
	 * The offsets are byte offsets into one vertex; indexSize is 2 or 4. Meshes loaded from
	 * formats without tangent attributes (OBJ, some FBX) used to reach the shaders with a zero
	 * tangent, which made the normal-map TBN degenerate. Generating them at load time fixes the
	 * data once for every shader instead of working around it per material.
	 */
	void GenerateTangents(
		void* p_vertexData,
		uint32_t p_vertexSize,
		const void* p_indexData,
		uint32_t p_indexSize,
		uint32_t p_indexStart,
		uint32_t p_indexCount,
		uint32_t p_normalOffset,
		uint32_t p_texCoordOffset,
		uint32_t p_tangentOffset
	);

	/** Fills the normal attribute of a vertex buffer from its faces (area weighted), for
	 * formats that have no normals at all. */
	void GenerateNormals(
		void* p_vertexData,
		uint32_t p_vertexSize,
		const void* p_indexData,
		uint32_t p_indexSize,
		uint32_t p_indexStart,
		uint32_t p_indexCount,
		uint32_t p_positionOffset,
		uint32_t p_normalOffset
	);
}
