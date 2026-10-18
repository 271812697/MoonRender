#include <Rendering/Overlay/Im3DGLRenderer.h>

#include <glad/glad.h>

#include <cstddef>

#include <Maths/FVector2.h>
#include <Rendering/Context/Driver.h>
#include <Rendering/Data/Material.h>
#include <Rendering/Settings/EBlendingEquation.h>
#include <Rendering/Settings/EBlendingFactor.h>
#include <Rendering/Settings/EComparaisonAlgorithm.h>
#include <Rendering/Settings/EOperation.h>

namespace
{
	// The overlay's vertex layout, in bytes: the editor's VertexFormat (position+size, colour,
	// normal, uv). Offsets are fixed here; the stride comes from the caller.
	constexpr int kPositionLocation = 0;
	constexpr int kColorLocation = 1;
	constexpr int kNormalLocation = 2;
	constexpr int kUvLocation = 3;
	constexpr size_t kPositionOffset = 0;
	constexpr size_t kColorOffset = 16;
	constexpr size_t kNormalOffset = 20;
	constexpr size_t kUvOffset = 32;
}

void Rendering::Overlay::Im3DGLRenderer::Initialize()
{
	if (m_vao != 0)
	{
		return;
	}

	glGenVertexArrays(1, &m_vao);
	glBindVertexArray(m_vao);
	glGenBuffers(1, &m_vbo);
	glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

	// The attribute pointers are described with a stride filled in at draw time; these are only
	// the offsets and formats, which do not change.
	glEnableVertexAttribArray(kPositionLocation);
	glVertexAttribPointer(kPositionLocation, 4, GL_FLOAT, GL_FALSE, 0, (GLvoid*)kPositionOffset);
	glEnableVertexAttribArray(kColorLocation);
	glVertexAttribPointer(kColorLocation, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, (GLvoid*)kColorOffset);
	glEnableVertexAttribArray(kNormalLocation);
	glVertexAttribPointer(kNormalLocation, 3, GL_FLOAT, GL_FALSE, 0, (GLvoid*)kNormalOffset);
	glEnableVertexAttribArray(kUvLocation);
	glVertexAttribPointer(kUvLocation, 2, GL_FLOAT, GL_FALSE, 0, (GLvoid*)kUvOffset);

	glBindVertexArray(0);
}

void Rendering::Overlay::Im3DGLRenderer::Begin(
	::Rendering::Context::Driver& p_driver,
	float p_viewportWidth, float p_viewportHeight, bool p_depthTest)
{
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
	pso.depthTest = p_depthTest;
	p_driver.SetPipelineState(pso);

	glEnable(GL_POINT_SPRITE);
	glEnable(GL_PROGRAM_POINT_SIZE);
	glViewport(0, 0, static_cast<GLsizei>(p_viewportWidth), static_cast<GLsizei>(p_viewportHeight));
}

void Rendering::Overlay::Im3DGLRenderer::Draw(
	::Rendering::Data::Material& p_material,
	unsigned int p_glPrimitive, bool p_isLinePass,
	const void* p_vertexData, size_t p_vertexCount, size_t p_vertexStride,
	const float p_viewProjMatrix[16],
	float p_viewportWidth, float p_viewportHeight)
{
	if (p_vertexCount == 0)
	{
		return;
	}

	glBindVertexArray(m_vao);
	glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

	// The stride is known here: re-describe the attributes with it before uploading.
	glVertexAttribPointer(kPositionLocation, 4, GL_FLOAT, GL_FALSE, (GLsizei)p_vertexStride, (GLvoid*)kPositionOffset);
	glVertexAttribPointer(kColorLocation, 4, GL_UNSIGNED_BYTE, GL_TRUE, (GLsizei)p_vertexStride, (GLvoid*)kColorOffset);
	glVertexAttribPointer(kNormalLocation, 3, GL_FLOAT, GL_FALSE, (GLsizei)p_vertexStride, (GLvoid*)kNormalOffset);
	glVertexAttribPointer(kUvLocation, 2, GL_FLOAT, GL_FALSE, (GLsizei)p_vertexStride, (GLvoid*)kUvOffset);

	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(p_vertexCount * p_vertexStride), p_vertexData, GL_STREAM_DRAW);

	if (p_isLinePass)
	{
		p_material.SetProperty("uViewport", Maths::FVector2{ p_viewportWidth, p_viewportHeight });
	}

	p_material.SetProperty("uViewProjMatrix", Maths::FMatrix4{
		p_viewProjMatrix[0], p_viewProjMatrix[1], p_viewProjMatrix[2], p_viewProjMatrix[3],
		p_viewProjMatrix[4], p_viewProjMatrix[5], p_viewProjMatrix[6], p_viewProjMatrix[7],
		p_viewProjMatrix[8], p_viewProjMatrix[9], p_viewProjMatrix[10], p_viewProjMatrix[11],
		p_viewProjMatrix[12], p_viewProjMatrix[13], p_viewProjMatrix[14], p_viewProjMatrix[15] });

	p_material.Bind();
	// Bind() only selects the program; this path bypasses ABaseRenderer::DrawEntity, so the
	// uniforms have to be pushed here.
	p_material.UploadProperties(true, true);

	glDrawArrays(p_glPrimitive, 0, (GLsizei)p_vertexCount);
}

void Rendering::Overlay::Im3DGLRenderer::BeginMeshes(::Rendering::Context::Driver& p_driver, bool p_blending)
{
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
	pso.blending = p_blending;
	pso.blendingEquation = ::Rendering::Settings::EBlendingEquation::FUNC_ADD;
	pso.blendingSrcFactor = ::Rendering::Settings::EBlendingFactor::SRC_ALPHA;
	pso.culling = 0;
	pso.depthTest = true;
	pso.depthFunc = ::Rendering::Settings::EComparaisonAlgorithm::GREATER_EQUAL;
	p_driver.SetPipelineState(pso);
}

void Rendering::Overlay::Im3DGLRenderer::DrawBoundTriangles(
	size_t p_vertexCount, int p_viewportX, int p_viewportY, int p_viewportWidth, int p_viewportHeight)
{
	if (p_vertexCount == 0)
	{
		return;
	}

	if (p_viewportWidth > 0 && p_viewportHeight > 0)
	{
		glViewport(p_viewportX, p_viewportY, p_viewportWidth, p_viewportHeight);
	}

	glDrawArrays(GL_TRIANGLES, 0, (GLsizei)p_vertexCount);
}