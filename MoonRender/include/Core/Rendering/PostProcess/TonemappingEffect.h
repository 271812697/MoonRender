#pragma once
#include <Rendering/Data/PipelineState.h>
#include <chrono>
#include <Core/Rendering/PostProcess/AEffect.h>
#include <Rendering/Data/Material.h>

namespace Core::Rendering::PostProcess
{
	enum class ETonemappingMode
	{
		NEUTRAL = 0,
		REINHARD = 1,
		REINHARD_JODIE = 2,
		UNCHARTED2 = 3,
		UNCHARTED2_FILMIC = 4,
		ACES = 5
	};

	struct TonemappingSettings : public Core::Rendering::PostProcess::EffectSettings
	{
		float exposure = 1.0f;
		ETonemappingMode mode = ETonemappingMode::NEUTRAL;
		bool gammaCorrection = true;
	};

	class TonemappingEffect : public AEffect
	{
	public:
		TonemappingEffect(::Rendering::Core::CompositeRenderer& p_renderer);
		virtual void Draw(
			::Rendering::Data::PipelineState p_pso,
			::Rendering::Data::Framebuffer& p_src,
			::Rendering::Data::Framebuffer& p_dst,
			const EffectSettings& p_settings
		) override;

	private:
		::Rendering::Data::Material m_tonemappingMaterial;
	};
}