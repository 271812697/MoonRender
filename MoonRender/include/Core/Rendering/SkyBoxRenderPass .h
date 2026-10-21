#pragma once
#include <Rendering/Core/ARenderPass.h>
#include <Rendering/HAL/Framebuffer.h>
#include <Core/Rendering/PostProcess/AEffect.h>
#include <Core/Resources/Material.h>
namespace Core::Rendering
{
	enum class SkyMode
	{
		Gradient=0,
		PureColor = 1,
		SkyBox=2,
	};
	struct SkyBoxSetting
	{
		SkyMode mode = SkyMode::Gradient;
		Maths::FVector4 topColor{ 0.211f,0.211f,0.211f,1.0f };
		Maths::FVector4 bottomColor{ 0.811f,0.811f,0.811f,1.0f };
		Maths::FVector4 clearColor{1,1,1,1};
	};
	class SkyboxRenderPass : public ::Rendering::Core::ARenderPass
	{
	public:
		SkyboxRenderPass(::Rendering::Core::CompositeRenderer& p_renderer);
		::Rendering::HAL::Texture* GetSkyBoxCube();
		::Rendering::HAL::Texture* GetIrradianceCube();
		::Rendering::HAL::Texture* GetPrefilterCube();
		::Rendering::HAL::Texture* GetBrdfTexture();
		void updateSkyTexture(const std::string& path); 
		SkyBoxSetting& GetSetting() { return mSetting; }

		/** Enables/disables building the IBL environment (HDR load, cubemap capture, irradiance
		 * and prefilter convolution, BRDF LUT). The material preview turns it off: it only wants
		 * the background, and the capture is the expensive half of this pass. */
		void SetEnvironmentCapture(bool p_enabled) { m_captureEnvironment = p_enabled; }
		bool IsEnvironmentCaptureEnabled() const { return m_captureEnvironment; }
		/** Scales the baked IBL radiance. The preview uses this to match Filament's IBL
		 * intensity without lifting the dark preview backdrop. */
		void SetEnvironmentIntensity(float p_intensity) { m_environmentIntensity = p_intensity; }
		float GetEnvironmentIntensity() const { return m_environmentIntensity; }

	protected:
		virtual void Draw(::Rendering::Data::PipelineState p_pso) override;
	private:
		void computeSkyTexture();
		/** Projects the sky's equirect HDR into the SH9 irradiance coefficients the IBL
		 * shaders read at binding point 4 (Filament's IndirectLight SH). */
		void computeIrradianceSH();
		
	private:
		::Core::Resources::Material m_skyboxMaterial;
		::Core::Resources::Material m_skyboxIrrandianceMaterial;
		::Core::Resources::Material m_skyboxPrefilterMaterial;
		::Core::Resources::Material m_brdfMaterial;
		::Core::Resources::Material m_clearMaterial;
		::Rendering::HAL::Framebuffer skyBoxBuffer;
		::Rendering::HAL::Framebuffer irradianceBuffer;
		::Rendering::HAL::Framebuffer prefilterBuffer;
		::Rendering::HAL::Framebuffer brdfBuffer;
		::Rendering::Resources::Texture*skyTexture=nullptr;
		std::shared_ptr<::Rendering::HAL::Texture> skyBoxCube;
		std::shared_ptr<::Rendering::HAL::Texture> irradianceCube;
		std::shared_ptr<::Rendering::HAL::Texture> prefilterCube;
		std::unique_ptr<::Rendering::HAL::UniformBuffer> m_irradianceSHBuffer;
		SkyBoxSetting mSetting;
		// The default environment is the same studio HDR the FilamentPBR material preview
		// bakes, so a material judged in the viewport and one judged in the preview share
		// their IBL. A scene/Setting-panel sky texture can still override it.
		std::string mPath = ":Textures/Studio.hdr";
		bool needUpdateSkyTexture = true;
		bool m_captureEnvironment = true;
		float m_environmentIntensity = 1.0f;
	};
}
