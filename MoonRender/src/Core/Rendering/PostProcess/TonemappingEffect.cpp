#include <Core/Global/ServiceLocator.h>
#include <Core/Rendering/PostProcess/TonemappingEffect.h>
#include <Core/ResourceManagement/ShaderManager.h>
#include <Rendering/HAL/Profiling.h>

Core::Rendering::PostProcess::TonemappingEffect::TonemappingEffect(::Rendering::Core::CompositeRenderer& p_renderer) : AEffect(p_renderer)
{
	m_tonemappingMaterial.SetShader(GetService(Core::ResourceManagement::ShaderManager)[":Shaders\\PostProcess\\Tonemapping.mat"]);
}

void Core::Rendering::PostProcess::TonemappingEffect::Draw(
	::Rendering::Data::PipelineState p_pso,
	::Rendering::HAL::Framebuffer& p_src,
	::Rendering::HAL::Framebuffer& p_dst,
	const EffectSettings& p_settings
)
{
	ZoneScoped;
	TracyGpuZone("TonemappingEffect");

	const auto& tonemappingSettings = static_cast<const TonemappingSettings&>(p_settings);

	// Tonemapping
	m_tonemappingMaterial.SetProperty("_Exposure", tonemappingSettings.exposure, true);
	m_tonemappingMaterial.SetProperty("_Mode", static_cast<int>(tonemappingSettings.mode), true);
	m_tonemappingMaterial.SetProperty("_GammaCorrection", static_cast<int>(tonemappingSettings.gammaCorrection), true);
	m_renderer.Blit(p_pso, p_src, p_dst, m_tonemappingMaterial);
}
