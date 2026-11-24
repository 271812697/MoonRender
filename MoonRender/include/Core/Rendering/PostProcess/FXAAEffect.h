#pragma once
#include <Rendering/Data/PipelineState.h>
#include <Core/Rendering/PostProcess/AEffect.h>
#include <Rendering/Data/Material.h>

namespace Core::Rendering::PostProcess
{
	struct FXAASettings : public EffectSettings {};

	class FXAAEffect : public AEffect
	{
	public:
		FXAAEffect(::Rendering::Core::CompositeRenderer& p_renderer);

		virtual void Draw(
			::Rendering::Data::PipelineState p_pso,
			::Rendering::Data::Framebuffer& p_src,
			::Rendering::Data::Framebuffer& p_dst,
			const EffectSettings& p_settings
		) override;

	private:
		::Rendering::Data::Material m_material;
	};
}
