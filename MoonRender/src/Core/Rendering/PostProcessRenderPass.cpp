#include <tracy/Tracy.hpp>
#include <Core/ECS/Components/CPostProcessStack.h>
#include <Core/Rendering/PostProcessRenderPass.h>
#include <Core/Global/ServiceLocator.h>
#include <Core/Rendering/FramebufferUtil.h>
#include <Core/Rendering/SceneRenderer.h>
#include <Core/ResourceManagement/ShaderManager.h>


#include <Rendering/Core/CompositeRenderer.h>
#include <Rendering/HAL/Profiling.h>

Core::Rendering::PostProcessRenderPass::PostProcessRenderPass(::Rendering::Core::CompositeRenderer& p_renderer) :
	::Rendering::Core::ARenderPass(p_renderer),
	m_pingPongBuffers{ "PostProcessBlit" }
{
	for (auto& buffer : m_pingPongBuffers.GetFramebuffers())
	{
		Core::Rendering::FramebufferUtil::SetupFramebuffer(
			buffer, 1, 1, false, false, false
		);
	}

	m_blitMaterial.SetShader(GetService(Core::ResourceManagement::ShaderManager)[":Shaders\\PostProcess\\Blit.shader"]);

	// Instantiate available effects
	m_effects.reserve(4);
	//m_effects.push_back(std::make_unique<Core::Rendering::PostProcess::AutoExposureEffect>(p_renderer));
	m_effects.push_back(std::make_unique<Core::Rendering::PostProcess::BloomEffect>(p_renderer));
	m_effects.push_back(std::make_unique<Core::Rendering::PostProcess::TonemappingEffect>(p_renderer));
	m_effects.push_back(std::make_unique<Core::Rendering::PostProcess::FXAAEffect>(p_renderer));
}

Tools::Utils::OptRef<const Core::Rendering::PostProcess::PostProcessStack> FindPostProcessStack(Core::SceneSystem::Scene& p_scene)
{
	auto& postProcessStacks = p_scene.GetFastAccessComponents().postProcessStacks;

	for (auto postProcessStack : postProcessStacks)
	{
		if (postProcessStack && postProcessStack->owner.IsActive())
		{
			return postProcessStack->GetStack();
		}
	}

	return std::nullopt;
}

void Core::Rendering::PostProcessRenderPass::Draw(::Rendering::Data::PipelineState p_pso)
{
	ZoneScoped;
	TracyGpuZone("PostProcessRenderPass");

	auto& sceneDescriptor = m_renderer.GetDescriptor<Core::Rendering::SceneRenderer::SceneDescriptor>();
	auto& scene = sceneDescriptor.scene;

	if (auto stack = FindPostProcessStack(scene))
	{
		auto& mssaaframebuffer = m_renderer.GetFrameDescriptor().outputMsaaBuffer.value();
		auto& presentbuffer = m_renderer.GetFrameDescriptor().presentBuffer.value();
		//msaa is not permitted to sample !so we turn to presentbuffer
		Core::Rendering::FramebufferUtil::CopyFramebufferColor(mssaaframebuffer, 0, presentbuffer, 0);
		//Core::Rendering::FramebufferUtil::CopyFramebufferColor(framebuffer,0, m_pingPongBuffers[0],0);
		m_renderer.Blit(p_pso, presentbuffer, m_pingPongBuffers[0], m_blitMaterial);

		for (auto& effect : m_effects)
		{
			const auto& settings = stack->Get(typeid(*effect));

			if (effect && effect->IsApplicable(settings))
			{
				effect->Draw(
					p_pso,
					m_pingPongBuffers[0],
					m_pingPongBuffers[1],
					settings
				);

				++m_pingPongBuffers;
			}
		}
		
		m_renderer.Blit(p_pso, m_pingPongBuffers[0], mssaaframebuffer, m_blitMaterial);
	}
}
