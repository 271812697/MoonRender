#pragma once
#include <cstddef>
#include <cstdint>

namespace Rendering::Context {
	class Driver;
}
namespace Rendering::Data {
	class Material;
}

namespace Rendering::Overlay
{
	/** Draws the overlay's immediate-mode geometry (gizmos, handles, guides, the view cube) with
	 * the driver.
	 *
	 * The geometry is a stream of vertices with position+size, a packed colour, a normal and a
	 * texture coordinate; it is uploaded every frame and drawn as points, lines or triangles with
	 * a material chosen by the caller. None of that needs a window toolkit - the driver, the
	 * GL objects and the materials are the render module's - so it lives here rather than in the
	 * editor's ImRenderer, which keeps building the geometry.
	 *
	 * The vertex array and buffer are *handles*: with a render thread the GL names belong to the
	 * executor, so the upload and the draw are recorded as commands (Filament's createVertexArray
	 * -> updateBufferObject -> draw). The vertex layout is the editor's VertexFormat: position+size
	 * at byte 0, colour at 16, normal at 20, uv at 32; the stride is passed in, so a layout change
	 * only has to be made in one place (the editor's struct). */
	class Im3DGLRenderer
	{
	public:
		~Im3DGLRenderer();

		/** Takes the vertex array and buffer handles and creates the GL objects on the side that
		 * owns the context. Safe to call twice. */
		void Initialize();

		/** Sets the pipeline state and viewport the overlay draws with, once per frame.
		 * p_depthTest false is what the 2D-ish passes (screen widgets) use. */
		void Begin(::Rendering::Context::Driver& p_driver,
			float p_viewportWidth, float p_viewportHeight, bool p_depthTest);

		/** Uploads p_vertexData and draws it with p_material.
		 * p_glPrimitive is GL_POINTS, GL_LINES or GL_TRIANGLES; p_isLinePass tells the shader the
		 * viewport (only the line shader uses it).
		 * p_extraFloats > 0 means each vertex carries that many floats after the base layout
		 * (a custom effect's per-vertex data); they are exposed as attribute 4 (and 5 for more
		 * than four), so a shader can read them without the base layout growing for everybody. */
		void Draw(::Rendering::Data::Material& p_material,
			unsigned int p_glPrimitive, bool p_isLinePass,
			const void* p_vertexData, size_t p_vertexCount, size_t p_vertexStride,
			const float p_viewProjMatrix[16],
			float p_viewportWidth, float p_viewportHeight,
			unsigned int p_extraFloats = 0);

		/** Sets the pipeline state the mesh passes use (the widgets' own meshes: depth test with
		 * greater-equal, stencil, blending on or off). */
		void BeginMeshes(::Rendering::Context::Driver& p_driver, bool p_blending);

		/** Draws from a vertex array somebody else filled - the widgets upload their meshes once
		 * and only draw them here (see PolygonMesh). A viewport width/height above zero sets the
		 * viewport first (the screen widgets draw into their own corner of the frame). */
		void DrawMesh(::Rendering::Context::Driver& p_driver,
			size_t p_vertexCount, uint32_t p_vertexArrayHandle,
			int p_viewportX = 0, int p_viewportY = 0, int p_viewportWidth = 0, int p_viewportHeight = 0);

		/** Sets the GL viewport. The mesh passes leave their own sub-viewport behind when they
		 * draw the navigation cube into a corner, so a pass that draws over the whole frame has
		 * to restore it explicitly. */
		void SetViewport(int p_x, int p_y, int p_width, int p_height);

	private:
		/** Slots in the backend's resource table, not GL names (see Initialize). */
		uint32_t m_vertexArrayHandle = 0;
		uint32_t m_vertexBufferHandle = 0;
	};
}
