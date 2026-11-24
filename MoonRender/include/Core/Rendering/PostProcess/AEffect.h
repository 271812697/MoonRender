#pragma once
#include <Rendering/Data/PipelineState.h>
#include <Rendering/Core/ARenderPass.h>
#include <Rendering/Data/Material.h>
#include <Rendering/Core/CompositeRenderer.h>

namespace Core::Rendering::PostProcess
{
	struct EffectSettings
	{
		bool enabled = true;
	};

	class AEffect
	{
	public:
		AEffect(::Rendering::Core::CompositeRenderer& p_renderer);


		virtual bool IsApplicable(const EffectSettings& p_settings) const;

		virtual void Draw(
			::Rendering::Data::PipelineState p_pso,
			::Rendering::Data::Framebuffer& p_src,
			::Rendering::Data::Framebuffer& p_dst,
			const EffectSettings& p_settings
		) = 0;

	protected:
		::Rendering::Core::CompositeRenderer& m_renderer;
	};
}
