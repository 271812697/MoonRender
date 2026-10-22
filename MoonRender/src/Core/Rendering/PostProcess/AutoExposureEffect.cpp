#include <Core/Global/ServiceLocator.h>
#include <Core/Rendering/PostProcess/AutoExposureEffect.h>
#include <Core/Rendering/FramebufferUtil.h>
#include <Core/ResourceManagement/ShaderManager.h>
#include <Rendering/HAL/Profiling.h>

constexpr uint32_t kLuminanceBufferResolution = 1024;
constexpr uint32_t kExposureBufferResolution = 1;

Core::Rendering::PostProcess::AutoExposureEffect::AutoExposureEffect(
	::Rendering::Core::CompositeRenderer& p_renderer
) :	AEffect(p_renderer),
	m_luminanceBuffer{ "Luminance" },
	m_exposurePingPongBuffer{ "Exposure" }
{
	for (auto& buffer : m_exposurePingPongBuffer.GetFramebuffers())
	{
		FramebufferUtil::SetupFramebuffer(
			buffer,
			kExposureBufferResolution,
			kExposureBufferResolution,
			false, false, false
		);
	}

	FramebufferUtil::SetupFramebuffer(
		m_luminanceBuffer,
		kLuminanceBufferResolution,
		kLuminanceBufferResolution,
		false, false,
		true // <-- use mipmaps
	);

	m_luminanceMaterial.SetShader(GetService(Core::ResourceManagement::ShaderManager)[":Shaders\\PostProcess\\Luminance.shader"]);
	m_exposureMaterial.SetShader(GetService(Core::ResourceManagement::ShaderManager)[":Shaders\\PostProcess\\AutoExposure.shader"]);
	m_compensationMaterial.SetShader(GetService(Core::ResourceManagement::ShaderManager)[":Shaders\\PostProcess\\ApplyExposure.shader"]);
}

void Core::Rendering::PostProcess::AutoExposureEffect::Draw(
	::Rendering::Data::PipelineState p_pso,
	::Rendering::HAL::Framebuffer& p_src,
	::Rendering::HAL::Framebuffer& p_dst,
	const EffectSettings& p_settings
)
{
	ZoneScoped;
	TracyGpuZone("AutoExposureEffect");

	const auto& autoExposureSettings = static_cast<const AutoExposureSettings&>(p_settings);

	// Luminance calculation
	m_luminanceBuffer.Resize(kLuminanceBufferResolution, kLuminanceBufferResolution);
	m_luminanceMaterial.SetProperty("_CenterWeightBias", autoExposureSettings.centerWeightBias, true);
	m_renderer.Blit(p_pso, p_src, m_luminanceBuffer, m_luminanceMaterial,
		::Rendering::Settings::EBlitFlags::DEFAULT & ~::Rendering::Settings::EBlitFlags::RESIZE_DST_TO_MATCH_SRC);
	const auto luminanceTex = m_luminanceBuffer.GetAttachment<::Rendering::HAL::Texture>(::Rendering::Settings::EFramebufferAttachment::COLOR);
	luminanceTex->GenerateMipmaps();

	float elapsedTime = 1.0f;
	auto currentTime = std::chrono::high_resolution_clock::now();
	if (m_previousTime.has_value())
	{
		elapsedTime = std::chrono::duration_cast<std::chrono::duration<float>>(
			currentTime - m_previousTime.value()
		).count();
	}
	m_previousTime = currentTime;

	auto& previousExposure = m_exposurePingPongBuffer[0];
	auto& currentExposure = m_exposurePingPongBuffer[1];
	++m_exposurePingPongBuffer;

	// Exposure adaptation
	m_exposureMaterial.SetProperty("_LuminanceTexture", &luminanceTex.value(), true);
	m_exposureMaterial.SetProperty("_MinLuminanceEV", autoExposureSettings.minLuminanceEV, true);
	m_exposureMaterial.SetProperty("_MaxLuminanceEV", autoExposureSettings.maxLuminanceEV, true);
	m_exposureMaterial.SetProperty("_ExposureCompensationEV", autoExposureSettings.exposureCompensationEV, true);
	m_exposureMaterial.SetProperty("_ElapsedTime", elapsedTime, true);
	m_exposureMaterial.SetProperty("_Progressive", static_cast<int>(autoExposureSettings.progressive && !m_firstFrame), true);
	m_exposureMaterial.SetProperty("_SpeedUp", autoExposureSettings.speedUp, true);
	m_exposureMaterial.SetProperty("_SpeedDown", autoExposureSettings.speedDown, true);
	m_renderer.Blit(p_pso, previousExposure, currentExposure, m_exposureMaterial);

	// Apply the exposure to the final image
	const auto exposureTex = currentExposure.GetAttachment<::Rendering::HAL::Texture>(::Rendering::Settings::EFramebufferAttachment::COLOR);
	m_compensationMaterial.SetProperty("_ExposureTexture", &exposureTex.value(), true);
	m_renderer.Blit(p_pso, p_src, p_dst, m_compensationMaterial);

	m_firstFrame = false;
}
