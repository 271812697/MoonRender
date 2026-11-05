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

#include "Context.h"

namespace Editor::Rendering
{
	/**
	* Draw a grid
	*/
	class GridRenderPass : public ::Rendering::Core::ARenderPass
	{
	public:
		struct GridDescriptor
		{
			Maths::FVector3 gridColor;
			Maths::FVector3 viewPosition;
			Maths::FVector3 mirrorPlaneNormal;
			Maths::FVector3 mirrorPlaneCenter;
		};

		/**
		* Constructor
		* @param p_renderer
		*/
		GridRenderPass(::Rendering::Core::CompositeRenderer& p_renderer);

	protected:
		virtual void Draw(::Rendering::Data::PipelineState p_pso) override;
		virtual void ResizeRenderer(int width, int height) override;
	private:
		::Rendering::HAL::Framebuffer m_mirroFbo;
		::Core::Rendering::PingPongFramebuffer  m_mirroShadowFbo;
		::Core::Resources::MaterialAsset m_gridMaterial;
		::Core::Resources::MaterialAsset m_ShadowMaterial;
		::Core::Resources::MaterialAsset m_BlurMaterial;
	};
}