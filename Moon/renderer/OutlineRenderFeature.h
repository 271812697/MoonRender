#pragma once
#include <Rendering/Entities/Camera.h>
#include <Rendering/Features/DebugShapeRenderFeature.h>
#include <Rendering/Core/CompositeRenderer.h>
#include <Core/ECS/Actor.h>
#include <Core/SceneSystem/SceneManager.h>
#include <Core/ECS/Components/CModelRenderer.h>
#include <Core/ECS/Components/CMaterialRenderer.h>
#include <Core/Resources/MaterialAsset.h>
#include <Core/ECS/Components/CAmbientBoxLight.h>
#include <Core/ECS/Components/CAmbientSphereLight.h>

#include "Context.h"

namespace Editor::Rendering
{
	/**
	* Draw the scene for actor picking
	*/
	class OutlineRenderFeature : public ::Rendering::Features::ARenderFeature
	{
	public:
		/**
		* Constructor
		* @param p_renderer
		* @param p_executionPolicy
		*/
		OutlineRenderFeature(
			::Rendering::Core::CompositeRenderer& p_renderer,
			::Rendering::Features::EFeatureExecutionPolicy p_executionPolicy
		);

		/**
		* Draw an outline around the given actor
		* @param p_actor
		* @param p_color
		* @param p_thickness
		*/
		virtual void DrawOutline(::Core::ECS::Actor& p_actor, const Maths::FVector4& p_color, float p_thickness);

	private:
		void DrawStencilPass(::Core::ECS::Actor& p_actor);
		void DrawOutlinePass(::Core::ECS::Actor& p_actor, const Maths::FVector4& p_color, float p_thickness);

		void DrawActorToStencil(::Rendering::Data::PipelineState p_pso, ::Core::ECS::Actor& p_actor);
		void DrawActorOutline(::Rendering::Data::PipelineState p_pso, ::Core::ECS::Actor& p_actor, const Maths::FVector4& p_color);
		void DrawModelToStencil(::Rendering::Data::PipelineState p_pso, const Maths::FMatrix4& p_worldMatrix, ::Rendering::Resources::Model& p_model, Tools::Utils::OptRef<const ::Core::ECS::Components::CMaterialRenderer::MaterialList> p_materials = std::nullopt);
		void DrawModelOutline(::Rendering::Data::PipelineState p_pso, const Maths::FMatrix4& p_worldMatrix, ::Rendering::Resources::Model& p_model, const Maths::FVector4& p_color, Tools::Utils::OptRef<const ::Core::ECS::Components::CMaterialRenderer::MaterialList > p_materials = std::nullopt);

	private:
		::Core::Resources::MaterialAsset m_stencilFillMaterial;
		::Core::Resources::MaterialAsset m_outlineMaterial;
	};
}