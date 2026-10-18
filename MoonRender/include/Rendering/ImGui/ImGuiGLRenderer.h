#pragma once
#include <ThirdParty/imgui/imgui/imgui.h>

namespace Rendering::ImGui
{
	/** Draws ImGui's draw data with plain GL, into whatever framebuffer is bound.
	 *
	 * Why it is here and not in the window layer: drawing ImGui is drawing. It is GL, a shader,
	 * a vertex array and a font texture - none of which needs a window toolkit. What does need
	 * one is feeding it input (mouse, keys, clipboard, cursor shape) and telling it how big the
	 * surface is; that stays in the window layer, and it calls this to get the pixels drawn.
	 *
	 * The GL objects belong to the context current when Initialize() runs - the render module's
	 * context, in the editor - so this draws correctly wherever that context renders. */
	class ImGuiGLRenderer
	{
	public:
		/** Creates the shader, the vertex array and the font texture. Needs a current GL context. */
		bool Initialize();
		void Shutdown();

		/** Draws p_drawData. p_framebufferWidth/Height are the target's size in pixels (they can
		 * differ from the logical display size on a high-DPI surface). */
		void Render(ImDrawData* p_drawData, int p_framebufferWidth, int p_framebufferHeight);

	private:
		bool CreateDeviceObjects();
		bool CreateFontsTexture();

		unsigned int m_fontTexture = 0;
		int m_shader = 0;
		int m_vertShader = 0;
		int m_fragShader = 0;
		int m_texUniform = 0;
		int m_projUniform = 0;
		int m_positionAttr = 0;
		int m_uvAttr = 0;
		int m_colorAttr = 0;
		unsigned int m_vbo = 0;
		unsigned int m_vao = 0;
		unsigned int m_ebo = 0;
	};
}
