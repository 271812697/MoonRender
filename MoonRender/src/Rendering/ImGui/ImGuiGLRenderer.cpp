#include <Rendering/ImGui/ImGuiGLRenderer.h>

#include <glad/glad.h>

#include <ThirdParty/imgui/imgui/imgui_freetype.h>

#include <Tools/Utils/PathParser.h>

#include <cstddef>
#include <iostream>

#ifdef GL_ES
#define IMGUI_GLSL_VERSION "#version 300 es\n"
#else
#define IMGUI_GLSL_VERSION "#version 330\n"
#endif

bool Rendering::ImGui::ImGuiGLRenderer::Initialize()
{
	return CreateDeviceObjects();
}

void Rendering::ImGui::ImGuiGLRenderer::Shutdown()
{
	if (m_vao != 0) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
	if (m_vbo != 0) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
	if (m_ebo != 0) { glDeleteBuffers(1, &m_ebo); m_ebo = 0; }
	if (m_shader != 0) { glDeleteProgram(m_shader); m_shader = 0; }
	if (m_fontTexture != 0) { glDeleteTextures(1, &m_fontTexture); m_fontTexture = 0; }
}

bool Rendering::ImGui::ImGuiGLRenderer::CreateFontsTexture()
{
	// Build the atlas: the editor's font, loaded with freetype for crisp small text.
	ImGuiIO& io = ::ImGui::GetIO();
	unsigned char* pixels = nullptr;
	int width = 0;
	int height = 0;

	ImFontConfig config;
	config.SizePixels = 25.0f;
	config.FontLoaderFlags |= ImGuiFreeTypeLoaderFlags_LightHinting;

	const std::string path = Tools::Utils::PathParser::GetExeDirectory() + "/Moon/Data/Engine/Fonts/Ruda-Bold.ttf";
	io.FontDefault = io.Fonts->AddFontFromFileTTF(path.c_str(), 25.0f, &config);
	io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

	GLint lastTexture = 0;
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &lastTexture);
	glGenTextures(1, &m_fontTexture);
	glBindTexture(GL_TEXTURE_2D, m_fontTexture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

	io.Fonts->TexID = m_fontTexture;
	glBindTexture(GL_TEXTURE_2D, lastTexture);
	return true;
}

bool Rendering::ImGui::ImGuiGLRenderer::CreateDeviceObjects()
{
	GLint lastTexture = 0;
	GLint lastArrayBuffer = 0;
	GLint lastVertexArray = 0;
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &lastTexture);
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &lastArrayBuffer);
	glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &lastVertexArray);

	const GLchar* vertexShader =
		IMGUI_GLSL_VERSION
		"uniform mat4 ProjMtx;\n"
		"in vec2 Position;\n"
		"in vec2 UV;\n"
		"in vec4 Color;\n"
		"out vec2 Frag_UV;\n"
		"out vec4 Frag_Color;\n"
		"void main()\n"
		"{\n"
		"	Frag_UV = UV;\n"
		"	Frag_Color = Color;\n"
		"	gl_Position = ProjMtx * vec4(Position.xy, 0, 1);\n"
		"}\n";

	const GLchar* fragmentShader =
		IMGUI_GLSL_VERSION
		"precision mediump float;"
		"uniform sampler2D Texture;\n"
		"in vec2 Frag_UV;\n"
		"in vec4 Frag_Color;\n"
		"out vec4 Out_Color;\n"
		"void main()\n"
		"{\n"
		"	Out_Color = Frag_Color * texture(Texture, Frag_UV.st);\n"
		"}\n";

	m_shader = glCreateProgram();
	m_vertShader = glCreateShader(GL_VERTEX_SHADER);
	m_fragShader = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(m_vertShader, 1, &vertexShader, 0);
	glShaderSource(m_fragShader, 1, &fragmentShader, 0);
	glCompileShader(m_vertShader);
	glCompileShader(m_fragShader);
	glAttachShader(m_shader, m_vertShader);
	glAttachShader(m_shader, m_fragShader);
	glLinkProgram(m_shader);

	m_texUniform = glGetUniformLocation(m_shader, "Texture");
	m_projUniform = glGetUniformLocation(m_shader, "ProjMtx");
	m_positionAttr = glGetAttribLocation(m_shader, "Position");
	m_uvAttr = glGetAttribLocation(m_shader, "UV");
	m_colorAttr = glGetAttribLocation(m_shader, "Color");

	glGenBuffers(1, &m_vbo);
	glGenBuffers(1, &m_ebo);

	glGenVertexArrays(1, &m_vao);
	glBindVertexArray(m_vao);
	glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
	glEnableVertexAttribArray(m_positionAttr);
	glEnableVertexAttribArray(m_uvAttr);
	glEnableVertexAttribArray(m_colorAttr);

#define OFFSETOF(TYPE, ELEMENT) ((size_t)&(((TYPE*)0)->ELEMENT))
	glVertexAttribPointer(m_positionAttr, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), (GLvoid*)OFFSETOF(ImDrawVert, pos));
	glVertexAttribPointer(m_uvAttr, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), (GLvoid*)OFFSETOF(ImDrawVert, uv));
	glVertexAttribPointer(m_colorAttr, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(ImDrawVert), (GLvoid*)OFFSETOF(ImDrawVert, col));
#undef OFFSETOF

	CreateFontsTexture();

	glBindTexture(GL_TEXTURE_2D, (GLuint)lastTexture);
	glBindBuffer(GL_ARRAY_BUFFER, (GLuint)lastArrayBuffer);
	glBindVertexArray((GLuint)lastVertexArray);

	return true;
}

void Rendering::ImGui::ImGuiGLRenderer::Render(ImDrawData* p_drawData, int p_framebufferWidth, int p_framebufferHeight)
{
	if (p_drawData == nullptr || p_framebufferWidth == 0 || p_framebufferHeight == 0)
	{
		return;
	}

	const ImGuiIO& io = ::ImGui::GetIO();
	// Screen coordinates to framebuffer coordinates (a high-DPI surface is bigger than the
	// logical size ImGui lays out in).
	p_drawData->ScaleClipRects(io.DisplayFramebufferScale);

	GLint lastActiveTexture = 0; glGetIntegerv(GL_ACTIVE_TEXTURE, &lastActiveTexture);
	glActiveTexture(GL_TEXTURE0);
	GLint lastProgram = 0; glGetIntegerv(GL_CURRENT_PROGRAM, &lastProgram);
	GLint lastTexture = 0; glGetIntegerv(GL_TEXTURE_BINDING_2D, &lastTexture);
	GLint lastArrayBuffer = 0; glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &lastArrayBuffer);
	GLint lastElementArrayBuffer = 0; glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &lastElementArrayBuffer);
	GLint lastVertexArray = 0; glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &lastVertexArray);
	GLint lastBlendSrcRgb = 0; glGetIntegerv(GL_BLEND_SRC_RGB, &lastBlendSrcRgb);
	GLint lastBlendDstRgb = 0; glGetIntegerv(GL_BLEND_DST_RGB, &lastBlendDstRgb);
	GLint lastBlendSrcAlpha = 0; glGetIntegerv(GL_BLEND_SRC_ALPHA, &lastBlendSrcAlpha);
	GLint lastBlendDstAlpha = 0; glGetIntegerv(GL_BLEND_DST_ALPHA, &lastBlendDstAlpha);
	GLint lastBlendEquationRgb = 0; glGetIntegerv(GL_BLEND_EQUATION_RGB, &lastBlendEquationRgb);
	GLint lastBlendEquationAlpha = 0; glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &lastBlendEquationAlpha);
	GLint lastViewport[4]; glGetIntegerv(GL_VIEWPORT, lastViewport);
	GLint lastScissorBox[4]; glGetIntegerv(GL_SCISSOR_BOX, lastScissorBox);
	GLboolean lastEnableBlend = glIsEnabled(GL_BLEND);
	GLboolean lastEnableCullFace = glIsEnabled(GL_CULL_FACE);
	GLboolean lastEnableDepthTest = glIsEnabled(GL_DEPTH_TEST);
	GLboolean lastEnableScissorTest = glIsEnabled(GL_SCISSOR_TEST);

	// Alpha blending, no culling, no depth, scissor on: ImGui's own state.
	glEnable(GL_BLEND);
	glBlendEquation(GL_FUNC_ADD);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_SCISSOR_TEST);

	glViewport(0, 0, p_framebufferWidth, p_framebufferHeight);
	const float orthoProjection[4][4] =
	{
		{ 2.0f / io.DisplaySize.x, 0.0f,                     0.0f, 0.0f },
		{ 0.0f,                    2.0f / -io.DisplaySize.y, 0.0f, 0.0f },
		{ 0.0f,                    0.0f,                    -1.0f, 0.0f },
		{ -1.0f,                   1.0f,                     0.0f, 1.0f },
	};
	glUseProgram(m_shader);
	glUniform1i(m_texUniform, 0);
	glUniformMatrix4fv(m_projUniform, 1, GL_FALSE, &orthoProjection[0][0]);
	glBindVertexArray(m_vao);

	const ImVec2 clipOff = p_drawData->DisplayPos;
	const ImVec2 clipScale = p_drawData->FramebufferScale;

	for (int n = 0; n < p_drawData->CmdListsCount; n++)
	{
		const ImDrawList* cmdList = p_drawData->CmdLists[n];
		const ImDrawIdx* idxBufferOffset = 0;

		glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
		glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)cmdList->VtxBuffer.Size * sizeof(ImDrawVert), (const GLvoid*)cmdList->VtxBuffer.Data, GL_STREAM_DRAW);

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)cmdList->IdxBuffer.Size * sizeof(ImDrawIdx), (const GLvoid*)cmdList->IdxBuffer.Data, GL_STREAM_DRAW);

		for (int cmdIndex = 0; cmdIndex < cmdList->CmdBuffer.Size; cmdIndex++)
		{
			const ImDrawCmd* cmd = &cmdList->CmdBuffer[cmdIndex];
			if (cmd->UserCallback)
			{
				cmd->UserCallback(cmdList, cmd);
			}
			else
			{
				// Clip rectangles go to framebuffer space; Y is inverted in GL.
				const ImVec2 clipMin((cmd->ClipRect.x - clipOff.x) * clipScale.x, (cmd->ClipRect.y - clipOff.y) * clipScale.y);
				const ImVec2 clipMax((cmd->ClipRect.z - clipOff.x) * clipScale.x, (cmd->ClipRect.w - clipOff.y) * clipScale.y);
				if (clipMax.x <= clipMin.x || clipMax.y <= clipMin.y)
				{
					continue;
				}

				glScissor((int)clipMin.x, (int)(p_framebufferHeight - clipMax.y), (int)(clipMax.x - clipMin.x), (int)(clipMax.y - clipMin.y));
				glBindTexture(GL_TEXTURE_2D, (GLuint)(size_t)cmd->GetTexID());
				glDrawElements(GL_TRIANGLES, (GLsizei)cmd->ElemCount,
					sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT, idxBufferOffset + cmd->IdxOffset);
			}
		}
	}

	// Put the caller's state back: this draws into somebody else's frame.
	glUseProgram(lastProgram);
	glBindTexture(GL_TEXTURE_2D, lastTexture);
	glActiveTexture(lastActiveTexture);
	glBindVertexArray(lastVertexArray);
	glBindBuffer(GL_ARRAY_BUFFER, lastArrayBuffer);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, lastElementArrayBuffer);
	glBlendEquationSeparate(lastBlendEquationRgb, lastBlendEquationAlpha);
	glBlendFuncSeparate(lastBlendSrcRgb, lastBlendDstRgb, lastBlendSrcAlpha, lastBlendDstAlpha);
	if (lastEnableBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
	if (lastEnableCullFace) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
	if (lastEnableDepthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
	if (lastEnableScissorTest) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
	glViewport(lastViewport[0], lastViewport[1], (GLsizei)lastViewport[2], (GLsizei)lastViewport[3]);
	glScissor(lastScissorBox[0], lastScissorBox[1], (GLsizei)lastScissorBox[2], (GLsizei)lastScissorBox[3]);
}
