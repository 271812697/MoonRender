#include <Core/ECS/Components/CMaterialRenderer.h>
#include <Core/Global/ServiceLocator.h>
#include <Core/Rendering/EngineBufferRenderFeature.h>
#include <Core/Rendering/EngineDrawableDescriptor.h>
#include <Core/Rendering/ShadowRenderPass.h>
#include <Core/ResourceManagement/ShaderManager.h>

#include <Rendering/Features/LightingRenderFeature.h>
#include <Rendering/HAL/Profiling.h>

constexpr uint8_t kMaxShadowMaps = 1;

Core::Rendering::ShadowRenderPass::ShadowRenderPass(::Rendering::Core::CompositeRenderer& p_renderer) :
	::Rendering::Core::ARenderPass(p_renderer)
{
	const auto shadowShader = GetService(Core::ResourceManagement::ShaderManager).GetResource(":Shaders\\ShadowFallback.mat");
	assert(shadowShader&& "Cannot find the shadow shader");

	m_shadowMaterial.SetShader(shadowShader);
	// No need to update the material settings, as its generated state mask will be erridden anyway.
}

void Core::Rendering::ShadowRenderPass::Draw(::Rendering::Data::PipelineState p_pso)
{
	ZoneScoped;
	TracyGpuZone("ShadowRenderPass");

	using namespace Core::Rendering;

	assert(m_renderer.HasDescriptor<SceneRenderer::SceneDescriptor>()&&"Cannot find SceneDescriptor attached to this renderer");
	assert(m_renderer.HasFeature<Core::Rendering::EngineBufferRenderFeature>()&&"Cannot find EngineBufferRenderFeature attached to this renderer");
	assert(m_renderer.HasDescriptor<::Rendering::Features::LightingRenderFeature::LightingDescriptor>()&&"Cannot find LightingDescriptor attached to this renderer");

	auto& engineBufferRenderFeature = m_renderer.GetFeature<Core::Rendering::EngineBufferRenderFeature>();
	auto& lightingDescriptor = m_renderer.GetDescriptor<::Rendering::Features::LightingRenderFeature::LightingDescriptor>();

	auto& sceneDescriptor = m_renderer.GetDescriptor<SceneRenderer::SceneDescriptor>();
	auto& frameDescriptor = m_renderer.GetFrameDescriptor();
	auto& scene = sceneDescriptor.scene;

	auto pso = m_renderer.CreatePipelineState();

	uint8_t lightIndex = 0;

	for (auto lightReference : lightingDescriptor.lights)
	{
		auto& light = lightReference.get();

		if (light.castShadows)
		{
			if (lightIndex < kMaxShadowMaps)
			{
				if (light.type == ::Rendering::Settings::ELightType::DIRECTIONAL)
				{
					light.PrepareForShadowRendering(frameDescriptor);

					engineBufferRenderFeature.SetCamera(light.shadowCamera.value());

					light.shadowBuffer->Bind();
					m_renderer.SetViewport(0, 0, light.shadowMapResolution, light.shadowMapResolution);
					m_renderer.Clear(true, true, true);
					_DrawShadows(pso, scene);
					light.shadowBuffer->Unbind();

					engineBufferRenderFeature.SetCamera(frameDescriptor.camera.value());
				}
				else
				{
					// Other light types not supported!
				}
			}
		}
	}

	if (auto output = frameDescriptor.outputMsaaBuffer)
	{
		output.value().Bind();
	}

	m_renderer.SetViewport(0, 0, frameDescriptor.renderWidth, frameDescriptor.renderHeight);
}

void Core::Rendering::ShadowRenderPass::_DrawShadows(
	::Rendering::Data::PipelineState p_pso,
	Core::SceneSystem::Scene& p_scene
)
{
	for (auto modelRenderer : p_scene.GetFastAccessComponents().modelRenderers)
	{
		auto& actor = modelRenderer->owner;

		if (actor.IsActive())
		{
			if (auto model = modelRenderer->GetModel())
			{
				if (auto materialRenderer = modelRenderer->owner.GetComponent<Core::ECS::Components::CMaterialRenderer>())
				{
					if (!materialRenderer->HasVisibilityFlags(EVisibilityFlags::SHADOW))
					{
						continue;
					}

					const auto& materials = materialRenderer->GetMaterials();
					const auto& modelMatrix = actor.transform.GetWorldMatrix();

					for (auto mesh : model->GetMeshes())
					{
						if (auto material = materials.at(mesh->GetMaterialIndex()[0]); material && material->IsValid() && material->IsShadowCaster())
						{
							const std::string shadowPassName = "SHADOW_PASS";

							// If the material has shadow pass, use it, otherwise use the shadow fallback material
							auto& targetMaterial =
								material->HasPass(shadowPassName) ?
								*material :
								m_shadowMaterial;

							::Rendering::Entities::Drawable drawable;
							drawable.mesh = *mesh;
							drawable.material = targetMaterial;

							// Generate the state mask for the target material, and erride
							// its properties to ensure the shadow pass is rendered correctly.
							drawable.stateMask = targetMaterial.GenerateStateMask();
							drawable.stateMask.blendable = false; // The shadow pass should never use blending.
							drawable.stateMask.depthTest = true; // The shadow pass should always use depth test.
							drawable.stateMask.colorWriting = false; // The shadow pass should never write color.
							drawable.stateMask.depthWriting = true; // The shadow pass should always write depth.

							// No front/backface culling for shadow pass (aka: two-sided shadow pass).
							// A "two-sided" shadow pass setting could be added in the future to change this behavior.
							drawable.stateMask.frontfaceCulling = false;
							drawable.stateMask.backfaceCulling = false;

							drawable.pass = shadowPassName;

							drawable.AddDescriptor<EngineDrawableDescriptor>({
								modelMatrix,
								materialRenderer->GetUserMatrix()
								});

							m_renderer.DrawEntity(p_pso, drawable);
						}
					}
				}
			}
		}
	}
}
