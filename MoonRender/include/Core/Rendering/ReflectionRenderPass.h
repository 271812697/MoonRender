#pragma once
#include <Rendering/Data/PipelineState.h>
#include <Rendering/Core/ARenderPass.h>
#include <Rendering/Data/Framebuffer.h>
#include <Core/Rendering/PostProcess/AEffect.h>
#include <Core/SceneSystem/Scene.h>

namespace Core::Rendering
{
	/**
	* Draw reflections
	*/
	class ReflectionRenderPass : public ::Rendering::Core::ARenderPass
	{
	public:
		/**
		* Constructor of the post-process render pass
		* @param p_renderer
		*/
		ReflectionRenderPass(::Rendering::Core::CompositeRenderer& p_renderer);

	protected:
		virtual void Draw(::Rendering::Data::PipelineState p_pso) override;

		void _DrawReflections(
			::Rendering::Data::PipelineState p_pso,
			const ::Rendering::Entities::Camera& p_camera
		);
	};
}
