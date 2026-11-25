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
		// Through the driver: the attachments are GL, and with a render thread they belong to the
		// side that owns the context.
		Core::Rendering::FramebufferUtil::SetupFramebufferWithDriver(
			buffer, 1, 1, false, false, false
		);
	}

	m_blitMaterial.SetMaterial(GetService(Core::ResourceManagement::ShaderManager)[":Shaders\\PostProcess\\Blit.shader"]);

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

		// Self-healing: the ping-pong targets were built when this pass was constructed, which can
		// be before the module's context existed. Their attachments then never landed, and every
		// bind reports a framebuffer with a missing attachment. If a target has no colour
		// attachment, build it now: the setup is a command, so it runs before the draws that follow
		// in the same batch.
		for (auto& buffer : m_pingPongBuffers.GetFramebuffers())
		{
			const bool hasColor = buffer.GetAttachment<::Rendering::Data::Texture>(
				::Rendering::Settings::EFramebufferAttachment::COLOR, 0).has_value();
			if (!hasColor)
			{
				Core::Rendering::FramebufferUtil::SetupFramebufferWithDriver(buffer, 1, 1, false, false, false);
			}
		}

		//msaa is not permitted to sample !so we turn to presentbuffer
		// Through the driver: the resolve is two blits of GL state on two targets, so it belongs to
		// the side that owns the context. The direct helper left presentbuffer holding the previous
		// frame, and the blit below then sampled that instead of the scene.
		m_renderer.GetDriver().CopyFramebufferColor(mssaaframebuffer, 0, presentbuffer, 0);
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
