#include <tracy/Tracy.hpp>

#include <Core/ECS/Components/CMaterialRenderer.h>
#include <Core/Rendering/ShadowRenderFeature.h>
#include <Rendering/Features/LightingRenderFeature.h>
#include <iostream>

constexpr uint8_t kMaxShadowMaps = 1;

Core::Rendering::ShadowRenderFeature::ShadowRenderFeature(
	::Rendering::Core::CompositeRenderer& p_renderer,
	::Rendering::Features::EFeatureExecutionPolicy p_executionPolicy
) :
	ARenderFeature(p_renderer, p_executionPolicy)
{
}

void Core::Rendering::ShadowRenderFeature::OnBeforeDraw(::Rendering::Data::PipelineState& p_pso, const ::Rendering::Entities::Drawable& p_drawable)
{
	ZoneScoped;

	auto& material = p_drawable.material.value();

	// Skip materials that aren't properly set to receive shadows.
	if (!material.IsShadowReceiver() || !material.HasProperty("_ShadowMap") || !material.HasProperty("_LightSpaceMatrix"))
	{
		return;
	}

	assert(m_renderer.HasDescriptor<::Rendering::Features::LightingRenderFeature::LightingDescriptor>()&&"Cannot find LightingDescriptor attached to this renderer");

	auto& lightDescriptor = m_renderer.GetDescriptor<::Rendering::Features::LightingRenderFeature::LightingDescriptor>();

	uint8_t lightIndex = 0;

	for (auto lightReference : lightDescriptor.lights)
	{
		const auto& light = lightReference.get();

		if (!light.castShadows) continue;

		if (lightIndex >= kMaxShadowMaps)
		{
			std::cout << ("ShadowRenderFeature does not support more than one shadow casting directional light at the moment") << std::endl;
			continue;
		}

		if (light.type == ::Rendering::Settings::ELightType::DIRECTIONAL)
		{
			assert(light.IsSetupForShadowRendering()&&"This light isn't setup for shadow rendering");

			const auto shadowTex = light.shadowBuffer->GetAttachment<::Rendering::HAL::Texture>(
				::Rendering::Settings::EFramebufferAttachment::DEPTH
			);

			material.SetProperty("_ShadowMap", &shadowTex.value(), true);
			material.SetProperty("_LightSpaceMatrix", light.lightSpaceMatrix.value(), true);
			// Tell the shader that this material is the one the map belongs to. A
			// material that is left without one must not look the shadow up at all -
			// the sampler would read whatever texture unit happens to be bound.
			material.SetProperty("u_ReceiveShadows", true, true);

			++lightIndex;
		}
	}
}

