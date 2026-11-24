#pragma once
#include <Rendering/Data/PipelineState.h>
#include <Core/Rendering/PostProcess/AEffect.h>
#include <Rendering/Data/Material.h>
#include <Rendering/Data/Framebuffer.h>

namespace Core::Rendering::PostProcess
{
	namespace BloomConstants
	{
		constexpr uint32_t kMinPassCount = 1;
		constexpr uint32_t kMaxPassCount = 10;
		constexpr float kMaxBloomIntensity = 1.0f / 0.04f; // 0.04f being the default internal interpolation factor.
	}

	/**
	* Settings for the BloomEffect
	*/
	struct BloomSettings : public EffectSettings
	{
		float intensity = 1.0f;
		int passes = 6;
	};

	/**
	* Bloom post-processing effect:
	* This effect will make the bright parts of the image glow
	*/
	class BloomEffect : public AEffect
	{
	public:
		/**
		* Constructor of the BloomEffect class
		* @param p_renderer
		*/
		BloomEffect(::Rendering::Core::CompositeRenderer& p_renderer);

		/**
		* Returns true if the effect is applicable with the given settings.
		* If the effect is not applicable, it will be skipped by the post processing render pass
		* @param p_settings
		*/
		virtual bool IsApplicable(const EffectSettings& p_settings) const override;

		/**
		* Render the bloom effect
		* @note: make sure the effect is applicable before calling this method
		* @param p_pso
		* @param p_src
		* @param p_dst
		* @param p_settings
		*/
		virtual void Draw(
			::Rendering::Data::PipelineState p_pso,
			::Rendering::Data::Framebuffer& p_src,
			::Rendering::Data::Framebuffer& p_dst,
			const EffectSettings& p_settings
		) override;

	private:
		std::array<::Rendering::Data::Framebuffer, BloomConstants::kMaxPassCount> m_bloomSamplingBuffers;
		::Rendering::Data::Framebuffer m_bloomOutputBuffer;
		::Rendering::Data::Material m_downsamplingMaterial;
		::Rendering::Data::Material m_upsamplingMaterial;
		::Rendering::Data::Material m_bloomMaterial;
		::Rendering::Data::Material m_blitMaterial;
	};
}