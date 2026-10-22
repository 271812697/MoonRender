#include <Core/Global/ServiceLocator.h>
#include <Core/Rendering/PostProcess/FXAAEffect.h>
#include <Core/ResourceManagement/ShaderManager.h>
#include <Rendering/HAL/Profiling.h>

Core::Rendering::PostProcess::FXAAEffect::FXAAEffect(::Rendering::Core::CompositeRenderer& p_renderer) : AEffect(p_renderer)
{
	m_material.SetShader(GetService(Core::ResourceManagement::ShaderManager)[":Shaders\\PostProcess\\FXAA.shader"]);
}

void Core::Rendering::PostProcess::FXAAEffect::Draw(
	::Rendering::Data::PipelineState p_pso,
	::Rendering::HAL::Framebuffer& p_src,
	::Rendering::HAL::Framebuffer& p_dst,
	const EffectSettings& p_settings
)
{
	ZoneScoped;
	TracyGpuZone("FXAAEffect");
	m_renderer.Blit(p_pso, p_src, p_dst, m_material);
}
