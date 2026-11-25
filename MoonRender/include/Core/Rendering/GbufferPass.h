#pragma once
#include <Rendering/Data/PipelineState.h>
#include <Rendering/Core/ARenderPass.h>
#include <Rendering/Data/Framebuffer.h>
#include <Core/Resources/MaterialAsset.h>
#include <Core/Rendering/PingPongFramebuffer.h>
namespace Core::Rendering
{
	struct GbufferTextureData {
		std::shared_ptr<::Rendering::Data::Texture> position;
		std::shared_ptr<::Rendering::Data::Texture> normal;
		std::shared_ptr<::Rendering::Data::Texture> albedo;
		std::shared_ptr<::Rendering::Data::Texture> occlusion;
		std::shared_ptr<::Rendering::Data::Texture> occlusionBlur;
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
		std::shared_ptr<::Rendering::Data::Texture> noiseTexture;
		::Core::Resources::MaterialAsset gbufferMaterial;
		::Core::Resources::MaterialAsset ssaoMaterial;
		::Core::Resources::MaterialAsset ssaoblurMaterial;
		GbufferTextureData gbufferData;
		::Rendering::Data::Framebuffer gbuffer;
		::Rendering::Data::Framebuffer ssaobuffer;
		::Rendering::Data::Framebuffer ssaoblurbuffer;
		GbufferParam gbufferParam;
	};
}
