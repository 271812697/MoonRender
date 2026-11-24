#pragma once
#include <Rendering/Data/PipelineState.h>
#include <Rendering/Core/ARenderPass.h>
#include <Rendering/Data/Framebuffer.h>
#include <Core/Rendering/PingPongFramebuffer.h>
#include <Core/Rendering/PostProcess/AEffect.h>

namespace Core::Rendering
{
	/**
	* Draw post-processing effects
	*/
	class PostProcessRenderPass : public ::Rendering::Core::ARenderPass
	{
	public:
		/**
		* Constructor of the post-process render pass
		* @param p_renderer
		*/
		PostProcessRenderPass(::Rendering::Core::CompositeRenderer& p_renderer);

	protected:
		virtual void Draw(::Rendering::Data::PipelineState p_pso) override;

	private:
		::Rendering::Data::Material m_blitMaterial;
		std::vector<std::unique_ptr<PostProcess::AEffect>> m_effects;
		PingPongFramebuffer m_pingPongBuffers;
	};
}
