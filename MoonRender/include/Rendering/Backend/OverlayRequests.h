#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Rendering::Backend
{
	/** The overlay's immediate geometry, uploaded and drawn where the context is.
	 *
	 * Im3D builds a stream of vertices every frame (gizmos, handles, guides, text runs) and the
	 * editor's VertexFormat is fixed: position+size (4 floats) at 0, colour (4 normalised bytes)
	 * at 16, normal (3 floats) at 20, uv (2 floats) at 32, stride given by the caller (the struct
	 * is 16-byte aligned and carries padding). None of that needs the recording thread, so the
	 * upload - attribute pointers included, because the stride changes per draw - is one command.
	 *
	 * A custom effect may append some floats per vertex; they are exposed as attributes 4 and 5
	 * and must be disabled again when a draw does not carry them.
	 */
	struct UploadOverlayVerticesRequest
	{
		uint32_t vertexArrayHandleId = 0;
		uint32_t vertexBufferHandleId = 0;
		std::vector<uint8_t> vertices;
		uint32_t stride = 0;
		uint32_t extraFloats = 0;
	};

	/** Binds a vertex array and draws p_vertexCount vertices from it.
	 *
	 * Used for geometry that was uploaded once and drawn many times (the view cube and the other
	 * polygon widgets) and for the immediate stream above. A viewport width/height above zero
	 * sets the viewport first: those widgets draw into their own corner of the frame. */
	struct DrawVertexArrayRequest
	{
		uint32_t vertexArrayHandleId = 0;
		uint32_t vertexCount = 0;
		/** GL_POINTS, GL_LINES or GL_TRIANGLES - the overlay works in GL enums. */
		uint32_t primitiveMode = 0;
		int32_t viewportX = 0;
		int32_t viewportY = 0;
		int32_t viewportWidth = 0;
		int32_t viewportHeight = 0;
	};

	/** One draw of the 2D overlay's list: an indexed range from its array, the texture it samples
	 * and the rectangle the list clipped it with.
	 *
	 * The texture is named one of two ways, decided on the recording side: `textureHandleId` is a
	 * front-end texture (resolved to its GL name where the context is), `textureName` is an object
	 * that already lives in the share group - the icon cache's textures are created by the window's
	 * context. The receiver never sees the draw list's own texture-id encoding.
	 */
	struct DrawIndexedVertexArrayRequest
	{
		uint32_t vertexArrayHandleId = 0;
		uint32_t indexCount = 0;
		/** In indices, not bytes. */
		uint32_t indexOffset = 0;
		/** GL_TRIANGLES in practice. */
		uint32_t primitiveMode = 0;
		uint32_t textureHandleId = 0;
		uint32_t textureName = 0;
		bool scissor = false;
		int32_t scissorX = 0;
		int32_t scissorY = 0;
		int32_t scissorWidth = 0;
		int32_t scissorHeight = 0;
	};

	/** A loose mat4 uniform on a program, by name: the 2D overlay's projection.
	 *
	 * Everything else in the engine reaches its uniforms through a material and its parameter
	 * block; the overlay has one program and one matrix, and both are named where the context is. */
	struct ProgramMatrix4Request
	{
		uint32_t programName = 0;
		std::string name;
		std::array<float, 16> value{};
	};
}
