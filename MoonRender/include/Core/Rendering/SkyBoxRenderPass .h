#pragma once
#include <Rendering/Core/ARenderPass.h>
#include <Rendering/HAL/Framebuffer.h>
#include <Core/Rendering/PostProcess/AEffect.h>
#include <Core/Resources/MaterialAsset.h>
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
		/** The two ends of the gradient, as the shader receives them. The tonemap compresses the
		 * bright end, so what reaches the screen is a few percent darker than these; the
		 * defaults are picked to land on a light studio backdrop (#E8EEF2 above, #CFD5DE below)
		 * once it has been through it. */
		Maths::FVector4 topColor{ 0.960f,0.972f,0.985f,1.0f };     // renders as ~#E8EBEE
		Maths::FVector4 bottomColor{ 0.830f,0.851f,0.892f,1.0f };  // renders as ~#CED3DE
		Maths::FVector4 clearColor{1,1,1,1};

		/** The gradient is fixed to the screen: the colour is a ramp in the pixel's height, so
		 * the backdrop stays exactly the same no matter where the camera looks. Taking the ramp
		 * from the view direction instead turns it with the camera, which is what made the
		 * background move (and its fixed dither pattern slide) while the view was dragged. */
		/** Where on the screen the two colours meet: 0 = the bottom row, 1 = the top one. */
		float gradientCenter = 0.5f;
		/** How much of the screen height the whole ramp covers: 1 runs from the top row to the
		 * bottom one, less keeps both colours flat over the rest of the screen. */
		float gradientSpread = 1.0f;
		/** > 1 keeps the dark end around longer and pushes the bright end towards the top. */
		float gradientCurve = 1.0f;
		/** Screen-space corner darkening, 0 = none; it is what gives the flat backdrop depth. */
		float gradientVignette = 0.06f;
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
		::Core::Resources::MaterialAsset m_skyboxMaterial;
		::Core::Resources::MaterialAsset m_skyboxIrrandianceMaterial;
		::Core::Resources::MaterialAsset m_skyboxPrefilterMaterial;
		::Core::Resources::MaterialAsset m_brdfMaterial;
		::Core::Resources::MaterialAsset m_clearMaterial;
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
