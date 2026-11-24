#include "Rendering/Features/LightingRenderFeature.h"
#include "Rendering/Core/CompositeRenderer.h"
#include <assert.h>

Rendering::Features::LightingRenderFeature::LightingRenderFeature(
	Core::CompositeRenderer& p_renderer,
	Rendering::Features::EFeatureExecutionPolicy p_executionPolicy,
	uint32_t p_bufferBindingPoint
) :
	ARenderFeature(p_renderer, p_executionPolicy),
	m_bufferBindingPoint(p_bufferBindingPoint)
{
	m_lightBuffer = std::make_unique<Rendering::Data::BufferObject>(
		Rendering::Settings::EBufferType::SHADER_STORAGE);
}

bool IsLightInFrustum(const Rendering::Entities::Light& p_light, const Rendering::Data::Frustum& p_frustum)
{
	const auto& position = p_light.transform->GetWorldPosition();
	const auto effectRange = p_light.CalculateEffectRange();

	// We always consider lights that have an +inf range (Not necessary to test if they are in frustum)
	const bool isOmniscientLight = std::isinf(effectRange);

	return
		isOmniscientLight ||
		p_frustum.SphereInFrustum(position.x, position.y, position.z, effectRange);
}

void Rendering::Features::LightingRenderFeature::Bind() const
{
	m_lightBuffer->Bind(m_bufferBindingPoint);
}

uint32_t Rendering::Features::LightingRenderFeature::GetBufferBindingPoint() const
{
	return m_bufferBindingPoint;
}

void Rendering::Features::LightingRenderFeature::OnBeginFrame(const Data::FrameDescriptor& p_frameDescriptor)
{
	assert(m_renderer.HasDescriptor<LightingDescriptor>()&&"Cannot find LightingDescriptor attached to this renderer");

	auto& lightDescriptor = m_renderer.GetDescriptor<LightingDescriptor>();
	auto& frameDescriptor = m_renderer.GetFrameDescriptor();

	std::vector<Maths::FMatrix4> lightMatrices;
	lightMatrices.reserve(lightDescriptor.lights.size());

	auto frustum = lightDescriptor.frustumOverride ?
		lightDescriptor.frustumOverride :
		frameDescriptor.camera->GetLightFrustum();

	for (auto light : lightDescriptor.lights)
	{
		if (!frustum || IsLightInFrustum(light.get(), frustum.value()))
		{
			lightMatrices.push_back(light.get().GenerateMatrix());
		}
	}

	const auto lightMatricesView = std::span{ lightMatrices };

	if (m_lightBuffer->Allocate(lightMatricesView.size_bytes(), Settings::EAccessSpecifier::STREAM_DRAW))
	{
		m_lightBuffer->Upload(lightMatricesView.data());
	}

	Bind();
}

void Rendering::Features::LightingRenderFeature::OnEndFrame()
{
	m_lightBuffer->Unbind();
}
