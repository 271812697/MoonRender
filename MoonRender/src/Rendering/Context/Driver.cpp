
#include <Rendering/Context/Driver.h>
#include <Rendering/HAL/Backend.h>
#include <Rendering/Utils/Conversions.h>
#include <tracy/Tracy.hpp>

namespace
{
	std::unique_ptr<Rendering::HAL::Backend> m_gfxBackend;
}

Rendering::Context::Driver::Driver(const Rendering::Settings::DriverSettings& p_driverSettings)
{
	m_gfxBackend = std::make_unique<Rendering::HAL::Backend>();

	auto initialPipelineState = m_gfxBackend->Init(p_driverSettings.debugMode);

	

	if (p_driverSettings.defaultPipelineState)
	{
		m_defaultPipelineState = p_driverSettings.defaultPipelineState.value();
	}
	
	m_pipelineState = initialPipelineState.value();
	SetPipelineState(m_defaultPipelineState);

	m_vendor = m_gfxBackend->GetVendor();
	m_hardware = m_gfxBackend->GetHardware();
	m_version = m_gfxBackend->GetVersion();
	m_shadingLanguageVersion = m_gfxBackend->GetShadingLanguageVersion();

}

Rendering::Context::Driver::~Driver()
{
}

void Rendering::Context::Driver::SetContextHost(IGLContextHost* p_contextHost)
{
	m_contextHost = p_contextHost;
}

Rendering::Context::IGLContextHost* Rendering::Context::Driver::GetContextHost() const
{
	return m_contextHost;
}

void Rendering::Context::Driver::ReapplyGlobalState()
{
	// Init also (re)loads the GL entry points, which keeps the pointers valid for the context
	// that is current now; the pipeline state it returns is the one we already have.
	(void)m_gfxBackend->Init(false);
}

void Rendering::Context::Driver::OnFrameCompleted()
{
	m_gfxBackend->OnFrameCompleted();

	// Prevents state leak between frames, and especially useful when external code (like ImGui)
	// requires a "neutral" pipeline state.
	ResetPipelineState();
}

void Rendering::Context::Driver::SetViewport(uint32_t p_x, uint32_t p_y, uint32_t p_width, uint32_t p_height)
{
	m_gfxBackend->SetViewport(p_x, p_y, p_width, p_height);
}

void Rendering::Context::Driver::Clear(
	bool p_colorBuffer,
	bool p_depthBuffer,
	bool p_stencilBuffer,
	const Maths::FVector4& p_color,
	bool p_scissor
)
{
	if (p_colorBuffer)
	{
		m_gfxBackend->SetClearColor(p_color.x, p_color.y, p_color.z, p_color.w);
	}

	auto pso = CreatePipelineState();

	if (p_stencilBuffer)
	{
		pso.stencilWriteMask = ~0;
	}

	pso.scissorTest = p_scissor;

	SetPipelineState(pso);

	m_gfxBackend->Clear(p_colorBuffer, p_depthBuffer, p_stencilBuffer);
}

void Rendering::Context::Driver::SetScissor(
	uint32_t p_x,
	uint32_t p_y,
	uint32_t p_width,
	uint32_t p_height
)
{
	m_gfxBackend->SetScissor(p_x, p_y, p_width, p_height);
}

void Rendering::Context::Driver::Draw(
	Data::PipelineState p_pso,
	const Resources::IMesh& p_mesh,
	Settings::EPrimitiveMode p_primitiveMode,
	uint32_t p_instances
)
{
	Draw(p_pso,p_mesh,0,p_primitiveMode,p_instances);
}

void Rendering::Context::Driver::Draw(Rendering::Data::PipelineState p_pso, const Resources::IMesh& p_mesh, int index, Settings::EPrimitiveMode p_primitiveMode, uint32_t p_instances)
{
	ZoneScoped;
	if (p_instances > 0)
	{
		SetPipelineState(p_pso);

		p_mesh.Bind(index);

		if (p_mesh.GetIndexCount() > 0)
		{
			//m_gfxBackend->DrawElementsInstanced(p_primitiveMode, p_mesh.GetIndexCount(index), p_instances);
			if (p_instances == 1)
			{
				m_gfxBackend->DrawElements(p_primitiveMode, p_mesh.GetIndexCount(index));
			}
			else
			{
				m_gfxBackend->DrawElementsInstanced(p_primitiveMode, p_mesh.GetIndexCount(index), p_instances);
			}
		}
		else
		{
			//m_gfxBackend->DrawArraysInstanced(p_primitiveMode, p_mesh.GetVertexCount(), p_instances);
			if (p_instances == 1)
			{
				m_gfxBackend->DrawArrays(p_primitiveMode, p_mesh.GetVertexCount());
			}
			else
			{
				m_gfxBackend->DrawArraysInstanced(p_primitiveMode, p_mesh.GetVertexCount(), p_instances);
			}
		}

		p_mesh.Unbind();
	}
}

void Rendering::Context::Driver::SetPipelineState(Rendering::Data::PipelineState p_state)
{
	ZoneScoped;
	using namespace Rendering::Settings;
	if (p_state.lineWidth != m_pipelineState.lineWidth) {
		m_gfxBackend->SetRasterizationLinesWidth(p_state.lineWidth);
		m_pipelineState.lineWidth = p_state.lineWidth;
	}

	if (p_state._bits != m_pipelineState._bits)
	{
		auto& i = p_state;
		auto& c = m_pipelineState;

		// Rasterization
		if (i.rasterizationMode != c.rasterizationMode) m_gfxBackend->SetRasterizationMode(i.rasterizationMode);
		if (i.lineWidthPow2 != c.lineWidthPow2) m_gfxBackend->SetRasterizationLinesWidth(Utils::Conversions::Pow2toFloat(i.lineWidthPow2));
		
		if (i.colorWriting.mask != c.colorWriting.mask) m_gfxBackend->SetColorWriting(i.colorWriting.r, i.colorWriting.g, i.colorWriting.b, i.colorWriting.a);
		if (i.depthWriting != c.depthWriting) m_gfxBackend->SetDepthWriting(i.depthWriting);

		if (i.blending != c.blending) m_gfxBackend->SetCapability(ERenderingCapability::BLEND, i.blending);
		if (i.culling != c.culling) m_gfxBackend->SetCapability(ERenderingCapability::CULL_FACE, i.culling);
		if (i.dither != c.dither) m_gfxBackend->SetCapability(ERenderingCapability::DITHER, i.dither);
		if (i.polygonOffsetFill != c.polygonOffsetFill) {
			m_gfxBackend->SetCapability(ERenderingCapability::POLYGON_OFFSET_FILL, i.polygonOffsetFill);
			if (i.polygonOffsetFill) {
				// Reversed-Z depth range: window depth is 1 at near and 0 at far,
				// so pushing a face "back" requires a NEGATIVE offset (positive
				// would move it toward the near plane and make it win over
				// coplanar lines).
				m_gfxBackend->PolygonOffset(-1.0f, -1.0f);
			}
		}
		//if (i.polygonOffsetLine != c.polygonOffsetLine) {
		//	m_gfxBackend->SetCapability(ERenderingCapability::POLYGON_OFFSET_LINE, i.polygonOffsetLine);
		//	if (i.polygonOffsetLine) {
		//		//m_gfxBackend->PolygonOffset(1.0f, 1.0f);
		//	}		
		//}

		
		if (i.sampleAlphaToCoverage != c.sampleAlphaToCoverage) m_gfxBackend->SetCapability(ERenderingCapability::SAMPLE_ALPHA_TO_COVERAGE, i.sampleAlphaToCoverage);
		if (i.depthTest != c.depthTest) m_gfxBackend->SetCapability(ERenderingCapability::DEPTH_TEST, i.depthTest);
		if (i.scissorTest != c.scissorTest) m_gfxBackend->SetCapability(ERenderingCapability::SCISSOR_TEST, i.scissorTest);
		if (i.stencilTest != c.stencilTest) m_gfxBackend->SetCapability(ERenderingCapability::STENCIL_TEST, i.stencilTest);
		if (i.multisample != c.multisample) m_gfxBackend->SetCapability(ERenderingCapability::MULTISAMPLE, i.multisample);

		// Stencil algorithm
		if (i.stencilFuncOp != c.stencilFuncOp ||
			i.stencilFuncRef != c.stencilFuncRef ||
			i.stencilFuncMask != c.stencilFuncMask)
			m_gfxBackend->SetStencilAlgorithm(i.stencilFuncOp, i.stencilFuncRef, i.stencilFuncMask);

		if (i.stencilWriteMask != c.stencilWriteMask) m_gfxBackend->SetStencilMask(i.stencilWriteMask);
		if (i.stencilOpFail != c.stencilOpFail || i.depthOpFail != c.depthOpFail || i.bothOpFail != c.bothOpFail) m_gfxBackend->SetStencilOperations(i.stencilOpFail, i.depthOpFail, i.bothOpFail);

		// Blending equation & function
		if (i.blendingEquation != c.blendingEquation) m_gfxBackend->SetBlendingEquation(i.blendingEquation);
		if (i.blendingSrcFactor != c.blendingSrcFactor ||
			i.blendingDestFactor != c.blendingDestFactor)
			m_gfxBackend->SetBlendingFunction(i.blendingSrcFactor, i.blendingDestFactor);

		// Depth
		if (i.depthFunc != c.depthFunc) m_gfxBackend->SetDepthAlgorithm(i.depthFunc);

		// Culling
		if (i.cullFace != c.cullFace) m_gfxBackend->SetCullFace(i.cullFace);

		m_pipelineState = p_state;
	}
}

void Rendering::Context::Driver::ResetPipelineState()
{
	SetPipelineState(m_defaultPipelineState);
}

Rendering::Data::PipelineState Rendering::Context::Driver::CreatePipelineState() const
{
	return m_defaultPipelineState;
}

std::string_view Rendering::Context::Driver::GetVendor() const
{
	return m_vendor;
}

std::string_view Rendering::Context::Driver::GetHardware() const
{
	return m_hardware;
}

std::string_view Rendering::Context::Driver::GetVersion() const
{
	return m_version;
}

std::string_view Rendering::Context::Driver::GetShadingLanguageVersion() const
{
	return m_shadingLanguageVersion;
}
