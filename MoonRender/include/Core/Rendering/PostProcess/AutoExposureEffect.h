#pragma once
#include <Rendering/Data/PipelineState.h>
#include <chrono>
#include <Core/Rendering/PingPongFramebuffer.h>
#include <Core/Rendering/PostProcess/AEffect.h>
#include <Rendering/Data/Material.h>

namespace Core::Rendering::PostProcess
{
	/**
	* Settings for the AutoExposure effect
	*/
	struct AutoExposureSettings : public EffectSettings
	{
		float centerWeightBias = 0.5f;
		float minLuminanceEV = -2.0f;
		float maxLuminanceEV = 16.0f;
		float exposureCompensationEV = 0.0f;
		bool progressive = true;
		float speedUp = 1.0f;
		float speedDown = 1.0f;
	};

	/**
	* Post-processing effect that adapts the exposure based on the scene luminance.
	*/
	class AutoExposureEffect : public AEffect
	{
	public:
		/**
		* Constructor of the AutoExposureEffect class
		* @param p_renderer
		*/
		AutoExposureEffect(::Rendering::Core::CompositeRenderer& p_renderer);

		/**
		* Render the Auto-Exposure effect
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
		std::optional<std::chrono::high_resolution_clock::time_point> m_previousTime;
		::Rendering::Data::Framebuffer m_luminanceBuffer;
		PingPongFramebuffer m_exposurePingPongBuffer;
		::Rendering::Data::Material m_luminanceMaterial;
		::Rendering::Data::Material m_exposureMaterial;
		::Rendering::Data::Material m_compensationMaterial;

		// Used to skip the first frame of the exposure adaptation
		bool m_firstFrame = true;
	};
}