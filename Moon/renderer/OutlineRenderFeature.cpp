#include <Core/ECS/Components/CMaterialRenderer.h>
#include <Core/Rendering/EngineDrawableDescriptor.h>
#include "DebugModelRenderFeature.h"
#include "Core/Global/ServiceLocator.h"
#include "OutlineRenderFeature.h"
#include <Rendering/Utils/Conversions.h>

namespace
{
	constexpr uint32_t kStencilMask = 0xFF;
	constexpr int32_t kStencilReference = 1;
	constexpr std::string_view kOutlinePassName = "OUTLINE_PASS";
}

Editor::Rendering::OutlineRenderFeature::OutlineRenderFeature(
	::Rendering::Core::CompositeRenderer& p_renderer,
	::Rendering::Features::EFeatureExecutionPolicy p_executionPolicy
) :
	::Rendering::Features::ARenderFeature(p_renderer, p_executionPolicy)
{
	/* Stencil Fill Material */
	m_stencilFillMaterial.SetShader(GetShaderService[":Shaders/OutlineFallback.ovfx"]);

	/* Outline Material */
	m_outlineMaterial.SetShader(GetShaderService[":Shaders/OutlineFallback.ovfx"]);
}

void Editor::Rendering::OutlineRenderFeature::DrawOutline(
	::Core::ECS::Actor& p_actor,
	const Maths::FVector4& p_color,
	float p_thickness
)
{
	DrawStencilPass(p_actor);
	DrawOutlinePass(p_actor, p_color, p_thickness);
}

void Editor::Rendering::OutlineRenderFeature::DrawStencilPass(::Core::ECS::Actor& p_actor)
{
	auto pso = m_renderer.CreatePipelineState();

	pso.stencilTest = true;
	pso.stencilWriteMask = kStencilMask;
	pso.stencilFuncRef = kStencilReference;
	pso.stencilFuncMask = kStencilMask;
	pso.stencilOpFail = ::Rendering::Settings::EOperation::REPLACE;
	pso.depthOpFail = ::Rendering::Settings::EOperation::REPLACE;
	pso.bothOpFail = ::Rendering::Settings::EOperation::REPLACE;
	pso.colorWriting.mask = 0x00;

	DrawActorToStencil(pso, p_actor);
}

void Editor::Rendering::OutlineRenderFeature::DrawOutlinePass(::Core::ECS::Actor& p_actor, const Maths::FVector4& p_color, float p_thickness)
{
	auto pso = m_renderer.CreatePipelineState();

	pso.stencilTest = true;
	pso.stencilOpFail = ::Rendering::Settings::EOperation::KEEP;
	pso.depthOpFail = ::Rendering::Settings::EOperation::KEEP;
	pso.bothOpFail = ::Rendering::Settings::EOperation::REPLACE;
	pso.stencilFuncOp = ::Rendering::Settings::EComparaisonAlgorithm::NOTEQUAL;
	pso.stencilFuncRef = kStencilReference;
	pso.stencilFuncMask = kStencilMask;
	pso.rasterizationMode = ::Rendering::Settings::ERasterizationMode::LINE;
	pso.lineWidthPow2 = ::Rendering::Utils::Conversions::FloatToPow2(p_thickness);

	DrawActorOutline(pso, p_actor, p_color);
}

void Editor::Rendering::OutlineRenderFeature::DrawActorToStencil(::Rendering::Data::PipelineState p_pso, ::Core::ECS::Actor& p_actor)
{
	if (p_actor.IsActive())
	{
		/* Render static mesh outline and bounding spheres */
		if (auto modelRenderer = p_actor.GetComponent<::Core::ECS::Components::CModelRenderer>(); modelRenderer && modelRenderer->GetModel())
		{
			if (auto materialRenderer = p_actor.GetComponent<::Core::ECS::Components::CMaterialRenderer>())
			{
				DrawModelToStencil(
					p_pso,
					p_actor.transform.GetWorldMatrix(),
					*modelRenderer->GetModel(),
					materialRenderer->GetMaterials()
				);
			}
		}

		/* Render camera component outline */
		if (auto cameraComponent = p_actor.GetComponent<::Core::ECS::Components::CCamera>(); cameraComponent)
		{
			auto translation = Maths::FMatrix4::Translation(p_actor.transform.GetWorldPosition());
			auto rotation = Maths::FQuaternion::ToMatrix4(p_actor.transform.GetWorldRotation());
			auto model = translation * rotation;
			DrawModelToStencil(p_pso, model, *GetModelService[":Models/Camera.fbx"]);
		}

		if (auto reflectionProbeComponent = p_actor.GetComponent<::Core::ECS::Components::CReflectionProbe>(); reflectionProbeComponent)
		{
			const auto translation = Maths::FMatrix4::Translation(
				p_actor.transform.GetWorldPosition() +
				reflectionProbeComponent->GetCapturePosition()
			);
			const auto rotation = Maths::FQuaternion::ToMatrix4(p_actor.transform.GetWorldRotation());
			const auto scale = Maths::FMatrix4::Scaling({ 0.5f, 0.5f, 0.5f });
			const auto model = translation * rotation * scale;
			DrawModelToStencil(p_pso, model, *GetModelService[":Models/Sphere.fbx"]);
		}

		for (auto& child : p_actor.GetChildren())
		{
			DrawActorToStencil(p_pso, *child);
		}
	}
}

void Editor::Rendering::OutlineRenderFeature::DrawActorOutline(
	::Rendering::Data::PipelineState p_pso,
	::Core::ECS::Actor& p_actor,
	const Maths::FVector4& p_color
)
{
	if (p_actor.IsActive())
	{
		if (auto modelRenderer = p_actor.GetComponent<::Core::ECS::Components::CModelRenderer>(); modelRenderer && modelRenderer->GetModel())
		{
			if (auto materialRenderer = p_actor.GetComponent<::Core::ECS::Components::CMaterialRenderer>())
			{
				DrawModelOutline(
					p_pso,
					p_actor.transform.GetWorldMatrix(),
					*modelRenderer->GetModel(),
					p_color,
					materialRenderer->GetMaterials()
				);
			}
		}

		if (auto cameraComponent = p_actor.GetComponent<::Core::ECS::Components::CCamera>(); cameraComponent)
		{
			auto translation = Maths::FMatrix4::Translation(p_actor.transform.GetWorldPosition());
			auto rotation = Maths::FQuaternion::ToMatrix4(p_actor.transform.GetWorldRotation());
			auto model = translation * rotation;
			DrawModelOutline(p_pso, model, *GetModelService[":Models/Camera.fbx"], p_color);
		}

		if (auto reflectionProbeComponent = p_actor.GetComponent<::Core::ECS::Components::CReflectionProbe>(); reflectionProbeComponent)
		{
			const auto translation = Maths::FMatrix4::Translation(
				p_actor.transform.GetWorldPosition() +
				reflectionProbeComponent->GetCapturePosition()
			);
			const auto rotation = Maths::FQuaternion::ToMatrix4(p_actor.transform.GetWorldRotation());
			const auto scale = Maths::FMatrix4::Scaling({ 0.5f, 0.5f, 0.5f });
			const auto model = translation * rotation * scale;
			DrawModelOutline(p_pso, model, *GetModelService[":Models/Sphere.fbx"], p_color);
		}

		for (auto& child : p_actor.GetChildren())
		{
			DrawActorOutline(p_pso, *child, p_color);
		}
	}
}

void Editor::Rendering::OutlineRenderFeature::DrawModelToStencil(
	::Rendering::Data::PipelineState p_pso,
	const Maths::FMatrix4& p_worldMatrix,
	::Rendering::Resources::Model& p_model,
	Tools::Utils::OptRef<const ::Core::ECS::Components::CMaterialRenderer::MaterialList> p_materials
)
{
	const std::string outlinePassName{ kOutlinePassName };

	for (auto mesh : p_model.GetMeshes())
	{
		auto getStencilMaterial = [&]() -> ::Core::Resources::Material& {
			auto material = p_materials.has_value() ? p_materials->at(mesh->GetMaterialIndex()[0]) : nullptr;
			if (material && material->IsValid() && material->HasPass(outlinePassName))
			{
				return *material;
			}
			return m_stencilFillMaterial;
			};

		auto& targetMaterial = getStencilMaterial();

		auto stateMask = targetMaterial.GenerateStateMask();

		auto engineDrawableDescriptor = ::Core::Rendering::EngineDrawableDescriptor{
			p_worldMatrix,
			Maths::FMatrix4::Identity
		};

		::Rendering::Entities::Drawable element;
		element.mesh = *mesh;
		element.material = targetMaterial;
		element.stateMask = stateMask;
		element.stateMask.depthTest = false;
		element.stateMask.colorWriting = false;
		element.pass = outlinePassName;

		element.AddDescriptor(engineDrawableDescriptor);

		m_renderer.DrawEntity(p_pso, element);
	}
}

void Editor::Rendering::OutlineRenderFeature::DrawModelOutline(
	::Rendering::Data::PipelineState p_pso,
	const Maths::FMatrix4& p_worldMatrix,
	::Rendering::Resources::Model& p_model,
	const Maths::FVector4& p_color,
	Tools::Utils::OptRef<const ::Core::ECS::Components::CMaterialRenderer::MaterialList> p_materials
)
{
	const std::string outlinePassName{ kOutlinePassName };

	for (auto mesh : p_model.GetMeshes())
	{
		auto getStencilMaterial = [&]() -> ::Core::Resources::Material& {
			auto material = p_materials.has_value() ? p_materials->at(mesh->GetMaterialIndex()[0]) : nullptr;
			if (material && material->IsValid() && material->HasPass(outlinePassName))
			{
				return *material;
			}
			return m_stencilFillMaterial;
			};

		auto& targetMaterial = getStencilMaterial();

		// Set the outline color property if it exists
		if (targetMaterial.GetProperty("_OutlineColor"))
		{
			targetMaterial.SetProperty("_OutlineColor", p_color, true);
		}

		auto stateMask = targetMaterial.GenerateStateMask();

		auto engineDrawableDescriptor = ::Core::Rendering::EngineDrawableDescriptor{
			p_worldMatrix,
			Maths::FMatrix4::Identity
		};

		::Rendering::Entities::Drawable drawable;
		drawable.mesh = *mesh;
		drawable.material = targetMaterial;
		drawable.stateMask = stateMask;
		drawable.stateMask.depthTest = false;
		drawable.pass = outlinePassName;

		drawable.AddDescriptor(engineDrawableDescriptor);

		m_renderer.DrawEntity(p_pso, drawable);
	}
}
