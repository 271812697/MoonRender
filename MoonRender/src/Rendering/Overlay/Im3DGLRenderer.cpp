#include <Rendering/Overlay/Im3DGLRenderer.h>

#include <glad/glad.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>

#include <Maths/FVector2.h>
#include <Rendering/Backend/BackendResources.h>
#include <Rendering/Backend/OverlayRequests.h>
#include <Rendering/Context/Driver.h>
#include <Rendering/Data/Material.h>
#include <Rendering/HAL/Common/HALDriverAccess.h>
#include <Rendering/Settings/EBlendingEquation.h>
#include <Rendering/Settings/EBlendingFactor.h>
#include <Rendering/Settings/EComparaisonAlgorithm.h>
#include <Rendering/Settings/EOperation.h>

namespace
{
	/** Records p_request for the executor, or - when this thread owns the context - runs it now
	 * through the driver's own entry point (the driver decides: inside a batch it runs inline). */
	Rendering::Context::Driver* CurrentDriver()
	{
		return ::Rendering::HAL::CurrentDriver();
	}
}

Rendering::Overlay::Im3DGLRenderer::~Im3DGLRenderer()
{
	// The names live on the executor: freeing the slot queues the GL deletion there.
	if (m_vertexArrayHandle != 0)
	{
		::Rendering::Backend::Records::DestroyVertexArray(
			::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwVertexArray>{ m_vertexArrayHandle });
		m_vertexArrayHandle = 0;
	}
	if (m_vertexBufferHandle != 0)
	{
		::Rendering::Backend::Records::DestroyBuffer(
			::Rendering::Backend::ResourceHandle<::Rendering::Backend::HwBufferObject>{ m_vertexBufferHandle });
		m_vertexBufferHandle = 0;
	}
}

void Rendering::Overlay::Im3DGLRenderer::Initialize()
{
	if (m_vertexArrayHandle != 0)
	{
		return;
	}

	// Filament's createVertexArray/createBuffer: the slot is taken here, the GL name is made on
	// the side that owns the context. The attribute pointers are set per upload (the stride
	// changes between draws), so nothing else has to happen at creation.
	using namespace ::Rendering::Backend;
	// Filament's createVertexArray: the driver reserves the handle and records the create.
	m_vertexArrayHandle = ::Rendering::Context::Driver::Get()
		.CreateVertexArrayObject("im3d").GetId();

	// The vertex buffer is the driver's: it reserves the handle and records the create.
	m_vertexBufferHandle = ::Rendering::Context::Driver::Get()
		.CreateBufferObject(::Rendering::Settings::EBufferType::VERTEX).GetId();

	if (std::getenv("MOON_FRAME_TRACE") != nullptr)
	{
		std::fprintf(stderr, "[ovl] init vao=%u vbo=%u\n", m_vertexArrayHandle, m_vertexBufferHandle);
		std::fflush(stderr);
	}
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

	// gl_PointSize from the shader, for the point sprites and the text runs: capabilities, i.e. the
	// executor's business like every other piece of GL state. POINT_SPRITE is the pre-core switch
	// for the same thing and is kept because the editor's context is a compatibility profile.
	p_driver.EnqueueCapability(::Rendering::Settings::ERenderingCapability::PROGRAM_POINT_SIZE, true);
	p_driver.EnqueueCapability(::Rendering::Settings::ERenderingCapability::POINT_SPRITE, true);
	p_driver.SetViewport(0, 0,
		static_cast<uint32_t>(p_viewportWidth), static_cast<uint32_t>(p_viewportHeight));
}

void Rendering::Overlay::Im3DGLRenderer::Draw(
	::Rendering::Data::Material& p_material,
	unsigned int p_glPrimitive, bool p_isLinePass,
	const void* p_vertexData, size_t p_vertexCount, size_t p_vertexStride,
	const float p_viewProjMatrix[16],
	float p_viewportWidth, float p_viewportHeight,
	unsigned int p_extraFloats)
{
	if (p_vertexCount == 0 || m_vertexArrayHandle == 0)
	{
		return;
	}

	::Rendering::Context::Driver* const driver = CurrentDriver();
	if (driver == nullptr)
	{
		return;
	}

	if (p_isLinePass)
	{
		p_material.SetProperty("uViewport", Maths::FVector2{ p_viewportWidth, p_viewportHeight });
	}

	p_material.SetProperty("uViewProjMatrix", Maths::FMatrix4{
		p_viewProjMatrix[0], p_viewProjMatrix[1], p_viewProjMatrix[2], p_viewProjMatrix[3],
		p_viewProjMatrix[4], p_viewProjMatrix[5], p_viewProjMatrix[6], p_viewProjMatrix[7],
		p_viewProjMatrix[8], p_viewProjMatrix[9], p_viewProjMatrix[10], p_viewProjMatrix[11],
		p_viewProjMatrix[12], p_viewProjMatrix[13], p_viewProjMatrix[14], p_viewProjMatrix[15] });

	// Both halves are GL, so they are recorded: the program and its uniforms (Bind records
	// BindProgram, UploadProperties records the descriptor work), the stream upload, and the draw.
	p_material.Bind();
	// Bind() only selects the program; this path bypasses ABaseRenderer::DrawEntity, so the
	// uniforms have to be pushed here.
	p_material.UploadProperties();

	::Rendering::Backend::UploadOverlayVerticesRequest upload;
	upload.vertexArrayHandleId = m_vertexArrayHandle;
	upload.vertexBufferHandleId = m_vertexBufferHandle;
	upload.stride = static_cast<uint32_t>(p_vertexStride);
	upload.extraFloats = p_extraFloats;
	upload.vertices.assign(static_cast<const uint8_t*>(p_vertexData),
		static_cast<const uint8_t*>(p_vertexData) + p_vertexCount * p_vertexStride);
	driver->EnqueueUploadOverlayVertices(std::move(upload));

	::Rendering::Backend::DrawVertexArrayRequest draw;
	draw.vertexArrayHandleId = m_vertexArrayHandle;
	draw.vertexCount = static_cast<uint32_t>(p_vertexCount);
	draw.primitiveMode = p_glPrimitive;
	driver->EnqueueDrawVertexArray(draw);
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

void Rendering::Overlay::Im3DGLRenderer::DrawMesh(::Rendering::Context::Driver& p_driver,
	size_t p_vertexCount, uint32_t p_vertexArrayHandle,
	int p_viewportX, int p_viewportY, int p_viewportWidth, int p_viewportHeight)
{
	if (p_vertexCount == 0 || p_vertexArrayHandle == 0)
	{
		return;
	}

	::Rendering::Backend::DrawVertexArrayRequest draw;
	draw.vertexArrayHandleId = p_vertexArrayHandle;
	draw.vertexCount = static_cast<uint32_t>(p_vertexCount);
	draw.primitiveMode = GL_TRIANGLES;
	draw.viewportX = p_viewportX;
	draw.viewportY = p_viewportY;
	draw.viewportWidth = p_viewportWidth;
	draw.viewportHeight = p_viewportHeight;
	p_driver.EnqueueDrawVertexArray(draw);
}

void Rendering::Overlay::Im3DGLRenderer::SetViewport(int p_x, int p_y, int p_width, int p_height)
{
	if (::Rendering::Context::Driver* const driver = CurrentDriver())
	{
		driver->SetViewport(
			static_cast<uint32_t>(std::max(0, p_x)),
			static_cast<uint32_t>(std::max(0, p_y)),
			static_cast<uint32_t>(std::max(0, p_width)),
			static_cast<uint32_t>(std::max(0, p_height)));
	}
}
