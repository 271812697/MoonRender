#include "DebugModelRenderFeature.h"
#include "Core/Rendering/EngineDrawableDescriptor.h"
#include "Rendering/Core/CompositeRenderer.h"

Editor::Rendering::DebugModelRenderFeature::DebugModelRenderFeature(
	::Rendering::Core::CompositeRenderer& p_renderer,
	::Rendering::Features::EFeatureExecutionPolicy p_executionPolicy
) :
	ARenderFeature(p_renderer, p_executionPolicy)
{
}

void Editor::Rendering::DebugModelRenderFeature::DrawModelWithSingleMaterial(::Rendering::Data::PipelineState p_pso, ::Rendering::Resources::Model& p_model, ::Rendering::Data::Material& p_material, const Maths::FMatrix4& p_modelMatrix)
{
	auto stateMask = p_material.GenerateStateMask();

	auto engineDrawableDescriptor = Core::Rendering::EngineDrawableDescriptor{
		p_modelMatrix,
		Maths::FMatrix4::Identity
	};

	for (auto mesh : p_model.GetMeshes())
	{
		::Rendering::Entities::Drawable element;
		element.mesh = *mesh;
		element.material = p_material.DefaultInstance();
		element.stateMask = stateMask;
		element.AddDescriptor(engineDrawableDescriptor);

		m_renderer.DrawEntity(p_pso, element);
	}
}
