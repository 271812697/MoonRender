#include <functional>
#include <ranges>
#include <tracy/Tracy.hpp>
#include <Core/Rendering/SkyBoxRenderPass .h>
#include <Rendering/Core/CompositeRenderer.h>
#include <Rendering/HAL/Profiling.h>

Rendering::Core::CompositeRenderer::CompositeRenderer(Context::Driver& p_driver)
	: ABaseRenderer(p_driver)
{
}
::Rendering::HAL::Texture* Rendering::Core::CompositeRenderer::GetSkyBoxCube() {
	return GetPass<::Core::Rendering::SkyboxRenderPass>("SkyboxRenderPass").GetSkyBoxCube();;
}
::Rendering::HAL::Texture* Rendering::Core::CompositeRenderer::GetIrradianceCube() {
	return GetPass<::Core::Rendering::SkyboxRenderPass>("SkyboxRenderPass").GetIrradianceCube();;
}
::Rendering::HAL::Texture* Rendering::Core::CompositeRenderer::GetPrefilterCube() {
	return GetPass<::Core::Rendering::SkyboxRenderPass>("SkyboxRenderPass").GetPrefilterCube();;
}
::Rendering::HAL::Texture* Rendering::Core::CompositeRenderer::GetBrdfTexture() {
	return GetPass<::Core::Rendering::SkyboxRenderPass>("SkyboxRenderPass").GetBrdfTexture();;
}
void Rendering::Core::CompositeRenderer::BeginFrame(const Data::FrameDescriptor& p_frameDescriptor)
{
	ZoneScoped;
	TracyGpuZone("BeginFrame");

	ABaseRenderer::BeginFrame(p_frameDescriptor);

	for (const auto& feature : m_features | std::views::values)
	{
		if (feature->IsEnabled())
		{
			feature->OnBeginFrame(p_frameDescriptor);
		}
	}

	for (const auto& pass : m_passes | std::views::values)
	{
		if (pass.second->IsEnabled())
		{
			pass.second->OnBeginFrame(p_frameDescriptor);
		}
	}
}

void Rendering::Core::CompositeRenderer::DrawFrame()
{
	ZoneScoped;
	TracyGpuZone("DrawFrame");
	// The DFG LUT is a view-global texture; materials declare _BRDFLut but must not carry it as
	// a per-instance property (a material without it sampled the white empty texture and lost
	// its whole diffuse term).
	SetBrdfLut(GetBrdfTexture());
	// Filament's IndirectLight: the sky pass bakes the irradiance and prefiltered cubes, and every
	// lit material reads them. They are view state, not material parameters, so they are bound
	// per draw here (DrawEntity) instead of relying on whoever created the material instance to
	// hand them over - a face material built before the first sky bake used to keep the empty
	// black cube forever, which reads as "black except where the sun reflects" as the view turns.
	SetEnvironmentTextures(GetIrradianceCube(), GetPrefilterCube());
	auto pso = CreatePipelineState();
	for (const auto& pass : m_passes | std::views::values)
	{
		m_currentPass = pass.second.get();

		m_frameDescriptor.outputMsaaBuffer.value().Bind();
		SetViewport(0, 0, m_frameDescriptor.renderWidth, m_frameDescriptor.renderHeight);

		if (m_currentPass->IsEnabled())
		{
			m_currentPass->Draw(pso);
		}

		m_currentPass.reset();
	}
}

void Rendering::Core::CompositeRenderer::EndFrame()
{
	ZoneScoped;
	TracyGpuZone("EndFrame");

	for (const auto& pass : m_passes | std::views::values)
	{
		if (pass.second->IsEnabled())
		{
			pass.second->OnEndFrame();
		}
	}

	for (const auto& feature : m_features | std::views::values)
	{
		if (feature->IsEnabled())
		{
			feature->OnEndFrame();
		}
	}

	ClearDescriptors();
	ABaseRenderer::EndFrame();
}

void Rendering::Core::CompositeRenderer::DrawEntity(
	Rendering::Data::PipelineState p_pso,
	const Entities::Drawable& p_drawable
)
{
	ZoneScoped;

	// Ensure the drawable is valid.
	// If not, skip the draw call and the attached features.
	if (!IsDrawable(p_drawable))
	{
		return;
	}
	const auto& currentPass = m_currentPass.value();
	const auto& passTypeId = typeid(currentPass);
	for (const auto& feature : m_features | std::views::values)
	{
		if (feature->IsEnabledFor(passTypeId))
		{
			feature->OnBeforeDraw(p_pso, p_drawable);
		}
	}

	ABaseRenderer::DrawEntity(p_pso, p_drawable);

	for (const auto& feature : m_features | std::views::values)
	{
		if (feature->IsEnabledFor(passTypeId))
		{
			feature->OnAfterDraw(p_drawable);
		}
	}
}
