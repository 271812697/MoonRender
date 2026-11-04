#pragma once
#include <Rendering/Core/ARenderPass.h>
#include <Rendering/HAL/Framebuffer.h>
#include <Core/Resources/MaterialAsset.h>
#include <Core/Rendering/PingPongFramebuffer.h>
namespace Core::Rendering
{
	struct GbufferTextureData {
		std::shared_ptr<::Rendering::HAL::Texture> position;
		std::shared_ptr<::Rendering::HAL::Texture> normal;
		std::shared_ptr<::Rendering::HAL::Texture> albedo;
		std::shared_ptr<::Rendering::HAL::Texture> occlusion;
		std::shared_ptr<::Rendering::HAL::Texture> occlusionBlur;
	};
	struct SSAOParam
	{
		float radius = 2.05;
		float bias = 0.025;
		float blurTolerance = 0.2f;
	};
	struct GbufferParam {
		SSAOParam ssaoParam;
	};
	class GbufferPass : public ::Rendering::Core::ARenderPass
	{
	public:
		GbufferPass(::Rendering::Core::CompositeRenderer& p_renderer);
		GbufferTextureData& GetGbufferData() { return gbufferData; }
		GbufferParam& getGbufferParam() { return gbufferParam; }
	protected:
		virtual void Draw(::Rendering::Data::PipelineState p_pso) override;
		virtual void ResizeRenderer(int width, int height) override;
	private:
		std::shared_ptr<::Rendering::HAL::Texture> noiseTexture;
		::Core::Resources::MaterialAsset gbufferMaterial;
		::Core::Resources::MaterialAsset ssaoMaterial;
		::Core::Resources::MaterialAsset ssaoblurMaterial;
		GbufferTextureData gbufferData;
		::Rendering::HAL::Framebuffer gbuffer;
		::Rendering::HAL::Framebuffer ssaobuffer;
		::Rendering::HAL::Framebuffer ssaoblurbuffer;
		GbufferParam gbufferParam;
	};
}
