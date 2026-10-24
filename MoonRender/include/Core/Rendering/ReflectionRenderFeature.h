#pragma once
#include <Core/ECS/Actor.h>
#include <Core/ECS/Components/CModelRenderer.h>
#include <Core/ECS/Components/CReflectionProbe.h>
#include <Core/Rendering/SceneRenderer.h>
#include <Core/Resources/Material.h>
#include <Core/SceneSystem/SceneManager.h>

#include <Rendering/Entities/Camera.h>
#include <Rendering/Features/DebugShapeRenderFeature.h>

namespace Core::Rendering
{
	/**
	* Ensures that reflection probes data is provided to
	* the appropriate drawable entities.
	*/
	class ReflectionRenderFeature : public ::Rendering::Features::ARenderFeature
	{
	public:
		/**
		* Provided by the scene renderer, this descriptor contains
		* reflection probes to evaluate and provide to the drawables.
		*/
		struct ReflectionDescriptor
		{
			std::vector<std::reference_wrapper<ECS::Components::CReflectionProbe>> reflectionProbes;
		};

		/**
		* Constructor
		* @param p_renderer
		* @param p_executionPolicy
		*/
		ReflectionRenderFeature(
			::Rendering::Core::CompositeRenderer& p_renderer,
			::Rendering::Features::EFeatureExecutionPolicy p_executionPolicy
		);

		/**
		* Prepare a given reflection probe for rendering.
		* @param p_reflectionProbe
		*/
		void PrepareProbe(Core::ECS::Components::CReflectionProbe& p_reflectionProbe);

		/**
		* Sends the reflection probe data to the material.
		* @param p_material
		* @param p_reflectionProbe (if set to nullopt, empty probe data will be sent)
		*/
		void SendProbeData(
			::Rendering::Data::MaterialInstance& p_material,
			Tools::Utils::OptRef<const Core::ECS::Components::CReflectionProbe> p_reflectionProbe
		);

		/**
		* Bind the reflection probe data to the proper binding point.
		* @param p_reflectionProbe
		* @param p_material
		*/
		void BindProbe(
			const Core::ECS::Components::CReflectionProbe& p_reflectionProbe
		);

	private:
		std::unique_ptr<::Rendering::HAL::UniformBuffer> m_defaultReflectionUbo;
	protected:
		virtual void OnBeginFrame(const ::Rendering::Data::FrameDescriptor& p_frameDescriptor) override;
		virtual void OnBeforeDraw(::Rendering::Data::PipelineState& p_pso, const ::Rendering::Entities::Drawable& p_drawable);
	};
}
