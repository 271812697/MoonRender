#pragma once
#include <Rendering/Entities/Camera.h>
#include <Rendering/Features/DebugShapeRenderFeature.h>
#include <Core/ECS/Actor.h>
#include <Core/SceneSystem/SceneManager.h>
#include <Core/ECS/Components/CModelRenderer.h>
#include <Core/Resources/MaterialAsset.h>
#include <Core/ECS/Components/CAmbientBoxLight.h>
#include <Core/ECS/Components/CAmbientSphereLight.h>
#include <Core/Rendering/SceneRenderer.h>

namespace Core::Rendering
{
	/**
	* Draw the scene to a depth buffer from the point of view of each light source
	*/
	class ShadowRenderPass : public ::Rendering::Core::ARenderPass
	{
	public:
		/**
		* Constructor
		* @param p_renderer
		*/
		ShadowRenderPass(::Rendering::Core::CompositeRenderer& p_renderer);

	private:
		virtual void Draw(::Rendering::Data::PipelineState p_pso) override;

		void _DrawShadows(
			::Rendering::Data::PipelineState p_pso,
			Core::SceneSystem::Scene& p_scene
		);

	private:
		Core::Resources::MaterialAsset m_shadowMaterial;
	};
}