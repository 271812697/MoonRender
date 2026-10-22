#include <Rendering/Overlay/Im2DRender.h>

#include <glad/glad.h>

#include <cstddef>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include <Tools/Utils/PathParser.h>

namespace
{
	/** The program is ImGui's renderer program with one addition: the texture is always
	 * sampled. Solid geometry (the draw list's texture id 0) is drawn with a 1x1 white texture,
	 * so there is exactly one code path and no "is this command textured" branch. */
	constexpr const char* kVertexShader = R"(#version 450 core
layout(location = 0) in vec2 Position;
layout(location = 1) in vec2 Uv;
layout(location = 2) in vec4 Color;

uniform mat4 ProjMtx;

out vec2 FragUv;
out vec4 FragColor;

void main()
{
	FragUv = Uv;
	FragColor = Color;
	gl_Position = ProjMtx * vec4(Position, 0.0, 1.0);
}
)";

	constexpr const char* kFragmentShader = R"(#version 450 core
in vec2 FragUv;
in vec4 FragColor;

uniform sampler2D Texture;

layout(location = 0) out vec4 OutColor;

void main()
{
	OutColor = FragColor * texture(Texture, FragUv);
}
)";

	GLuint CompileShader(GLenum p_type, const char* p_source)
	{
		const GLuint shader = glCreateShader(p_type);
		glShaderSource(shader, 1, &p_source, nullptr);
		glCompileShader(shader);
		GLint ok = GL_FALSE;
		glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
		if (ok == GL_FALSE)
		{
			char log[2048] = {};
			glGetShaderInfoLog(shader, sizeof(log) - 1, nullptr, log);
			std::cerr << "[Im2D] shader compile failed: " << log << std::endl;
			glDeleteShader(shader);
			return 0;
		}
		return shader;
	}

	/** The font file the UI uses, plus fallbacks for the glyphs it does not carry (the editor's
	 * labels are Chinese and the primary face is a Latin one). A missing fallback is not fatal:
	 * the atlas simply has no glyph for those codepoints, and the next face is tried. */
	std::vector<std::string> FontPaths()
	{
		const std::string exeDir = Tools::Utils::PathParser::GetExeDirectory();
		return {
			exeDir + "/Moon/Data/Engine/Fonts/Ruda-Bold.ttf",
			"C:/Windows/Fonts/msyh.ttc",
			"C:/Windows/Fonts/segoeui.ttf",
		};
	}
}

class Rendering::Overlay::Im2DRender::Internal
{
public:
	explicit Internal(Im2DRender* p_owner) : m_owner(p_owner)
	{
	}

	~Internal()
	{
		if (!m_fontTextures.empty())
		{
			glDeleteTextures(static_cast<GLsizei>(m_fontTextures.size()), m_fontTextures.data());
		}
		if (m_whiteTexture != 0)
		{
			glDeleteTextures(1, &m_whiteTexture);
		}
		if (m_program != 0)
		{
			glDeleteProgram(m_program);
		}
		if (m_vertexBuffer != 0)
		{
			glDeleteBuffers(1, &m_vertexBuffer);
		}
		if (m_indexBuffer != 0)
		{
			glDeleteBuffers(1, &m_indexBuffer);
		}
		if (m_vertexArray != 0)
		{
			glDeleteVertexArrays(1, &m_vertexArray);
		}
	}

	/** Creates the GL objects and loads the font. Needs a current context: it runs from
	 * NewFrame, which the render loop calls with the module's context current. */
	void Init()
	{
		if (m_initialised)
		{
			return;
		}
		m_initialised = true;

		const GLuint vs = CompileShader(GL_VERTEX_SHADER, kVertexShader);
		const GLuint fs = CompileShader(GL_FRAGMENT_SHADER, kFragmentShader);
		if (vs == 0 || fs == 0)
		{
			return;
		}
		m_program = glCreateProgram();
		glAttachShader(m_program, vs);
		glAttachShader(m_program, fs);
		glLinkProgram(m_program);
		glDeleteShader(vs);
		glDeleteShader(fs);
		m_projUniform = glGetUniformLocation(m_program, "ProjMtx");
		m_textureUniform = glGetUniformLocation(m_program, "Texture");

		glGenVertexArrays(1, &m_vertexArray);
		glGenBuffers(1, &m_vertexBuffer);
		glGenBuffers(1, &m_indexBuffer);
		glBindVertexArray(m_vertexArray);
		glBindBuffer(GL_ARRAY_BUFFER, m_vertexBuffer);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_indexBuffer);
		constexpr GLsizei stride = static_cast<GLsizei>(sizeof(Im2D::DrawVert));
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride,
			reinterpret_cast<GLvoid*>(offsetof(Im2D::DrawVert, pos)));
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride,
			reinterpret_cast<GLvoid*>(offsetof(Im2D::DrawVert, uv)));
		glEnableVertexAttribArray(2);
		glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride,
			reinterpret_cast<GLvoid*>(offsetof(Im2D::DrawVert, col)));
		glBindVertexArray(0);

		// 1x1 white: what untextured geometry samples.
		const uint8_t white[4] = { 255, 255, 255, 255 };
		glGenTextures(1, &m_whiteTexture);
		glBindTexture(GL_TEXTURE_2D, m_whiteTexture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);


		m_font.Load(FontPaths(), kFontPixelSize);
		if (!m_font.IsLoaded())
		{
			std::cerr << "[Im2D] no font could be loaded; text will be empty" << std::endl;
		}
		// The atlases themselves are created on demand in NewFrame: the font makes a new one
		// whenever a text size appears for the first time.


		glBindTexture(GL_TEXTURE_2D, 0);
	}

	/** Makes sure every size atlas the font has so far has a GL texture, and re-uploads the ones
	 * that gained glyphs. One 1 MB upload per touched size is cheap next to the per-glyph
	 * sub-uploads a dirty-rect scheme would need to track. */
	void SyncFontAtlases()
	{
		if (!m_font.IsLoaded())
		{
			return;
		}
		const int atlasCount = m_font.GetAtlasCount();
		while (static_cast<int>(m_fontTextures.size()) < atlasCount)
		{
			GLuint texture = 0;
			glGenTextures(1, &texture);
			glBindTexture(GL_TEXTURE_2D, texture);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_font.GetAtlasWidth(0), m_font.GetAtlasHeight(0),
				0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
			m_fontTextures.push_back(texture);
			const int index = static_cast<int>(m_fontTextures.size()) - 1;
			m_font.SetTextureId(index, static_cast<Im2D::TextureId>(texture));

			// The dirty flag is deliberately NOT consumed here: the loop below has to upload the
			// pixels, and a freshly made atlas always has some (it was created by baking a
			// glyph). Eating the flag here left the texture allocated but never filled.
		}
		for (int i = 0; i < atlasCount; ++i)
		{
			if (!m_font.ConsumeAtlasDirty(i))
			{
				continue;
			}
			glBindTexture(GL_TEXTURE_2D, m_fontTextures[static_cast<size_t>(i)]);
			glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_font.GetAtlasWidth(i), m_font.GetAtlasHeight(i),
				GL_RGBA, GL_UNSIGNED_BYTE, m_font.GetAtlasPixels(i).data());
		}
	}

	void UploadGeometry(const Im2D::DrawList& p_list)
	{
		glBindBuffer(GL_ARRAY_BUFFER, m_vertexBuffer);
		glBufferData(GL_ARRAY_BUFFER,
			static_cast<GLsizeiptr>(p_list.GetVertices().size() * sizeof(Im2D::DrawVert)),
			p_list.GetVertices().data(), GL_STREAM_DRAW);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_indexBuffer);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER,
			static_cast<GLsizeiptr>(p_list.GetIndices().size() * sizeof(uint32_t)),
			p_list.GetIndices().data(), GL_STREAM_DRAW);
	}

	Im2DRender* m_owner = nullptr;
	Im2D::DrawList m_drawList;
	Im2D::Font m_font;
	GLuint m_program = 0;
	GLuint m_vertexArray = 0;
	GLuint m_vertexBuffer = 0;
	GLuint m_indexBuffer = 0;
	GLuint m_whiteTexture = 0;
	std::vector<GLuint> m_fontTextures;
	GLint m_projUniform = -1;
	GLint m_textureUniform = -1;
	bool m_initialised = false;
	static constexpr float kFontPixelSize = 24.0f;
};

Rendering::Overlay::Im2DRender::Im2DRender()
	: mInternal(new Internal(this))
{
}

Rendering::Overlay::Im2DRender::~Im2DRender()
{
	delete mInternal;
}

Rendering::Overlay::Im2DRender& Rendering::Overlay::Im2DRender::Instance()
{
	static Im2DRender render2D;
	return render2D;
}

Rendering::Overlay::Im2D::DrawList& Rendering::Overlay::Im2DRender::GetDrawList()
{
	return mInternal->m_drawList;
}

Rendering::Overlay::Im2D::Font& Rendering::Overlay::Im2DRender::GetFont()
{
	return mInternal->m_font;
}

void Rendering::Overlay::Im2DRender::NewFrame()
{
	mInternal->Init();
	mInternal->m_drawList.Reset();
	// Glyphs are baked on demand while the frame is recorded, so the textures are synced here
	// (a size that appears for the first time mid-frame is uploaded on the next one).
	mInternal->SyncFontAtlases();
}

void Rendering::Overlay::Im2DRender::EndFrame(
	::Rendering::Context::Driver& p_driver, uint16_t p_viewWidth, uint16_t p_viewHeight)
{
	// Closes the run the last primitive opened; without it the tail of the frame is dropped.
	mInternal->m_drawList.AddDrawCommand();

	const auto& commands = mInternal->m_drawList.GetCommands();
	if (commands.empty() || mInternal->m_program == 0 || p_viewWidth == 0 || p_viewHeight == 0)
	{
		return;
	}


	mInternal->UploadGeometry(mInternal->m_drawList);

	GLint lastProgram = 0; glGetIntegerv(GL_CURRENT_PROGRAM, &lastProgram);
	GLint lastTexture = 0; glGetIntegerv(GL_TEXTURE_BINDING_2D, &lastTexture);
	GLint lastVertexArray = 0; glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &lastVertexArray);
	GLint lastArrayBuffer = 0; glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &lastArrayBuffer);
	GLint lastElementArrayBuffer = 0; glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &lastElementArrayBuffer);
	GLint lastViewport[4]; glGetIntegerv(GL_VIEWPORT, lastViewport);
	GLint lastScissorBox[4]; glGetIntegerv(GL_SCISSOR_BOX, lastScissorBox);
	GLboolean lastEnableBlend = glIsEnabled(GL_BLEND);
	GLboolean lastEnableCullFace = glIsEnabled(GL_CULL_FACE);
	GLboolean lastEnableDepthTest = glIsEnabled(GL_DEPTH_TEST);
	GLboolean lastEnableScissorTest = glIsEnabled(GL_SCISSOR_TEST);

	glEnable(GL_BLEND);
	glBlendEquation(GL_FUNC_ADD);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_SCISSOR_TEST);

	glViewport(0, 0, p_viewWidth, p_viewHeight);

	// Screen space (origin top-left, y down) to clip space.
	const float left = 0.0f;
	const float right = static_cast<float>(p_viewWidth);
	const float top = 0.0f;
	const float bottom = static_cast<float>(p_viewHeight);
	const float ortho[16] =
	{
		2.0f / (right - left), 0.0f,                  0.0f,  0.0f,
		0.0f,                  2.0f / (top - bottom), 0.0f,  0.0f,
		0.0f,                  0.0f,                 -1.0f,  0.0f,
		(right + left) / (left - right), (top + bottom) / (bottom - top), 0.0f, 1.0f,
	};

	glUseProgram(mInternal->m_program);
	glUniformMatrix4fv(mInternal->m_projUniform, 1, GL_FALSE, ortho);
	glUniform1i(mInternal->m_textureUniform, 0);
	glBindVertexArray(mInternal->m_vertexArray);
	glActiveTexture(GL_TEXTURE0);

	for (const auto& cmd : commands)
	{
		if (cmd.elementCount == 0)
		{
			continue;
		}
		// The scissor rectangle is in pixels from the bottom-left; the draw list clips from the
		// top-left, hence the flip.
		const int x0 = static_cast<int>(cmd.clipRect.x);
		const int y0 = static_cast<int>(static_cast<float>(p_viewHeight) - cmd.clipRect.w);
		const int x1 = static_cast<int>(cmd.clipRect.z);
		const int y1 = static_cast<int>(static_cast<float>(p_viewHeight) - cmd.clipRect.y);
		glScissor(x0, y0, x1 - x0, y1 - y0);

		const GLuint texture = cmd.textureId != 0
			? static_cast<GLuint>(cmd.textureId)
			: mInternal->m_whiteTexture;
		glBindTexture(GL_TEXTURE_2D, texture);


		// The indices in the buffer are already absolute vertex indices (the draw list writes
		// them against the whole buffer, not against each command), so the command only selects
		// a range of them: no base vertex here. Adding vertexOffset as well would shift every
		// index a second time and drop the command off the end of the buffer.
		glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(cmd.elementCount), GL_UNSIGNED_INT,
			reinterpret_cast<void*>(static_cast<uintptr_t>(cmd.indexOffset * sizeof(uint32_t))));
	}


	glBindVertexArray(static_cast<GLuint>(lastVertexArray));
	glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(lastArrayBuffer));
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLuint>(lastElementArrayBuffer));
	glUseProgram(static_cast<GLuint>(lastProgram));
	glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(lastTexture));
	glViewport(lastViewport[0], lastViewport[1], lastViewport[2], lastViewport[3]);
	glScissor(lastScissorBox[0], lastScissorBox[1], lastScissorBox[2], lastScissorBox[3]);
	if (lastEnableScissorTest) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
	if (lastEnableBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
	if (lastEnableCullFace) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
	if (lastEnableDepthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
}
