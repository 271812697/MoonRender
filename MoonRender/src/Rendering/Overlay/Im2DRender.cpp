#include <Rendering/Overlay/Im2DRender.h>

#include <glad/glad.h>

#include <array>
#include <cstring>
#include <iostream>
#include <string>

#include <Maths/FMatrix4.h>
#include <Rendering/Context/Driver.h>
#include <Rendering/Data/Material.h>
#include <Rendering/HAL/IndexBuffer.h>
#include <Rendering/HAL/VertexArray.h>
#include <Rendering/HAL/VertexBuffer.h>
#include <Rendering/Resources/Loaders/ShaderLoader.h>
#include <Rendering/Settings/EBlendingEquation.h>
#include <Rendering/Settings/EBlendingFactor.h>
#include <Rendering/Settings/EComparaisonAlgorithm.h>
#include <Rendering/Settings/EOperation.h>
#include <Rendering/Settings/VertexAttribute.h>

namespace
{
	const std::string kVertexShader = R"(
#version 450 core
layout(location = 0) in vec2 Position;
layout(location = 1) in vec2 Uv;
layout(location = 2) in vec4 Color;

uniform mat4 ProjMtx;

out vec2 Frag_UV;
out vec4 Frag_Color;

void main()
{
    Frag_UV = Uv;
    Frag_Color = Color;
    gl_Position = ProjMtx * vec4(Position, 0.0, 1.0);
}
)";

	const std::string kFragmentShader = R"(
#version 450 core

in vec2 Frag_UV;
in vec4 Frag_Color;
uniform sampler2D Texture;
layout (location = 0) out vec4 Out_Color;
void main()
{
    Out_Color = Frag_Color;
};
)";
}

class Rendering::Overlay::Im2DRender::Internal
{
public:
	explicit Internal(Im2DRender* p_owner) : m_owner(p_owner)
	{
	}

	~Internal()
	{
		delete m_vertexArray;
		delete m_vertexBuffer;
		delete m_indexBuffer;
	}

	/** Creates the shader and the buffers. Needs a current context: it is called from NewFrame,
	 * which the render loop calls with the module's context current. */
	void Init()
	{
		if (m_initialised)
		{
			return;
		}
		m_initialised = true;

		m_shader = ::Rendering::Resources::Loaders::ShaderLoader::CreateFromSource(kVertexShader, kFragmentShader);
		m_material.SetShader(m_shader);

		m_vertexArray = new ::Rendering::HAL::VertexArray();
		m_indexBuffer = new ::Rendering::HAL::IndexBuffer();
		m_vertexBuffer = new ::Rendering::HAL::VertexBuffer();
		m_drawList = new ImDrawList(::ImGui::GetDrawListSharedData());

		m_vertexArray->SetLayout(
			std::to_array<::Rendering::Settings::VertexAttribute>(
				{
					{ ::Rendering::Settings::EDataType::FLOAT, 2 },                    // position
					{ ::Rendering::Settings::EDataType::FLOAT, 2 },                    // texCoords
					{ ::Rendering::Settings::EDataType::UNSIGNED_BYTE, 4, true }       // colours
				}), *m_vertexBuffer, *m_indexBuffer);
	}

	void Upload()
	{
		if (m_vertexBuffer->Allocate(m_drawList->VtxBuffer.size() * sizeof(ImDrawVert),
			::Rendering::Settings::EAccessSpecifier::STREAM_DRAW))
		{
			m_vertexBuffer->Upload(m_drawList->VtxBuffer.Data);
			if (m_indexBuffer->Allocate(m_drawList->IdxBuffer.size() * sizeof(unsigned int),
				::Rendering::Settings::EAccessSpecifier::STREAM_DRAW))
			{
				m_indexBuffer->Upload(m_drawList->IdxBuffer.Data);
			}
		}
	}

	Im2DRender* m_owner = nullptr;
	/** Created with the ImGui context's shared data: an ImDrawList cannot be default-built. */
	ImDrawList* m_drawList = nullptr;
	::Rendering::Resources::Shader* m_shader = nullptr;
	::Rendering::Data::Material m_material;
	::Rendering::HAL::VertexBuffer* m_vertexBuffer = nullptr;
	::Rendering::HAL::IndexBuffer* m_indexBuffer = nullptr;
	::Rendering::HAL::VertexArray* m_vertexArray = nullptr;
	bool m_initialised = false;
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

ImDrawList* Rendering::Overlay::Im2DRender::GetDrawList()
{
	return mInternal->m_drawList;
}

void Rendering::Overlay::Im2DRender::NewFrame()
{
	if (!mInternal->m_initialised)
	{
		mInternal->Init();
	}

	// A fresh list each frame, sharing the ImGui context's data: this imgui exposes no public
	// clear on ImDrawList, and _ResetForNewFrame is what sets up the write pointers and the
	// full-screen clip that a bare constructor leaves at zero (drawing into such a list crashes).
	delete mInternal->m_drawList;
	mInternal->m_drawList = new ImDrawList(::ImGui::GetDrawListSharedData());
	mInternal->m_drawList->_ResetForNewFrame();
}

void Rendering::Overlay::Im2DRender::EndFrame(
	::Rendering::Context::Driver& p_driver, uint16_t p_viewWidth, uint16_t p_viewHeight)
{
	mInternal->Upload();

	auto pso = p_driver.CreatePipelineState();
	pso.stencilTest = true;
	pso.stencilWriteMask = 0xFF;
	pso.stencilFuncRef = 1;
	pso.stencilFuncMask = 0xFF;
	pso.stencilOpFail = ::Rendering::Settings::EOperation::REPLACE;
	pso.depthOpFail = ::Rendering::Settings::EOperation::REPLACE;
	pso.bothOpFail = ::Rendering::Settings::EOperation::REPLACE;
	pso.colorWriting.mask = 0x00;
	pso.depthWriting = true;
	pso.colorWriting.mask = 0xFF;
	pso.blending = true;
	pso.blendingEquation = ::Rendering::Settings::EBlendingEquation::FUNC_ADD;
	pso.blendingSrcFactor = ::Rendering::Settings::EBlendingFactor::SRC_ALPHA;
	pso.depthFunc = ::Rendering::Settings::EComparaisonAlgorithm::ALWAYS;
	pso.culling = 0;
	pso.depthTest = false;
	p_driver.SetPipelineState(pso);
	mInternal->m_vertexArray->Bind();

	// Screen space to clip space, with Y pointing down (a draw list's origin is the top left).
	const float left = 0.0f;
	const float right = static_cast<float>(p_viewWidth);
	const float top = 0.0f;
	const float bottom = static_cast<float>(p_viewHeight);

	const float orthoProjection[4][4] =
	{
		{ 2.0f / (right - left), 0.0f,                  0.0f,  0.0f },
		{ 0.0f,                  2.0f / (top - bottom), 0.0f,  0.0f },
		{ 0.0f,                  0.0f,                 -1.0f,  0.0f },
		{ (right + left) / (left - right), (top + bottom) / (bottom - top), 0.0f, 1.0f },
	};

	Maths::FMatrix4 projMat;
	std::memcpy(projMat.data, orthoProjection, 16 * sizeof(float));
	projMat = projMat.TransposeMartix();

	mInternal->m_material.SetProperty("ProjMtx", projMat);
	mInternal->m_material.Bind();
	// Bind() only selects the program: without this the projection matrix never reaches the
	// shader and the whole draw list collapses to nothing.
	mInternal->m_material.UploadProperties(true, true);

	for (size_t i = 0; i < mInternal->m_drawList->CmdBuffer.size(); i++)
	{
		auto& cmd = mInternal->m_drawList->CmdBuffer[i];
		glDrawElementsBaseVertex(
			GL_TRIANGLES, (GLsizei)cmd.ElemCount, GL_UNSIGNED_INT,
			(void*)(intptr_t)(cmd.IdxOffset * sizeof(ImDrawIdx)), (GLint)cmd.VtxOffset);
	}

	mInternal->m_material.Unbind();
	mInternal->m_vertexArray->Unbind();
}
