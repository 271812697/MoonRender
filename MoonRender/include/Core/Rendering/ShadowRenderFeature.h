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
	* Ensures drawable are properly setup to receive shadows by
	* priding them with the shadow map and light-space matrix.
	*/
	class ShadowRenderFeature : public ::Rendering::Features::ARenderFeature
	{
	public:
		/**
		* Constructor
		* @param p_renderer
		* @param p_executionPolicy
		*/
		ShadowRenderFeature(
			::Rendering::Core::CompositeRenderer& p_renderer,
			::Rendering::Features::EFeatureExecutionPolicy p_executionPolicy
		);

	protected:
		virtual void OnBeforeDraw(::Rendering::Data::PipelineState& p_pso, const ::Rendering::Entities::Drawable& p_drawable);
	};
}