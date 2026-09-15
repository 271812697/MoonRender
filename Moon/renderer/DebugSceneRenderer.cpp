#include <Core/ECS/Components/CCamera.h>
#include <Core/ECS/Components/CDirectionalLight.h>
#include <Core/ECS/Components/CMaterialRenderer.h>

#include <Core/ECS/Components/CPointLight.h>
#include <Core/ECS/Components/CSpotLight.h>
#include <Core/Rendering/EngineDrawableDescriptor.h>
#include <Core/Rendering/ReflectionRenderFeature.h>

#include "EditorResources.h"
#include "AView.h"

#include "DebugModelRenderFeature.h"
#include "DebugSceneRenderer.h"
#include "PointRenderPass.h"
#include "GizmoRenderPass.h"
#include "GridRenderPass.h"
#include "OutlineRenderFeature.h"
#include "PickingRenderPass.h"
#include "PathTraceRenderPass.h"

#include "Core/Global/ServiceLocator.h"
#include "Settings/DebugSetting.h"
#include "renderer/SceneView.h"
#include <Rendering/Features/DebugShapeRenderFeature.h>
#include <Rendering/Features/FrameInfoRenderFeature.h>
#include <Rendering/Features/LightingRenderFeature.h>
#include <Rendering/HAL/Profiling.h>

using namespace Maths;
using namespace Rendering::Resources;
using namespace Core::Resources;

namespace
{
	const Maths::FVector3 kDebugBoundsColor = { 1.0f, 0.0f, 0.0f };
	const Maths::FVector3 kLightVolumeColor = { 1.0f, 1.0f, 0.0f };
	const Maths::FVector3 kColliderColor = { 0.0f, 1.0f, 0.0f };
	const Maths::FVector3 kFrustumColor = { 1.0f, 1.0f, 1.0f };

	const Maths::FVector4 kHeredOutlineColor{ 1.0f, 1.0f, 0.0f, 1.0f };
	const Maths::FVector4 kSelectedOutlineColor{ 1.0f, 0.7f, 0.0f, 1.0f };

	constexpr float kHeredOutlineWidth = 2.5f;
	constexpr float kSelectedOutlineWidth = 5.0f;

	Maths::FMatrix4 CalculateUnscaledModelMatrix(Core::ECS::Actor& p_actor)
	{
		auto translation = FMatrix4::Translation(p_actor.transform.GetWorldPosition());
		auto rotation = FQuaternion::ToMatrix4(p_actor.transform.GetWorldRotation());
		return translation * rotation;
	}

	std::optional<std::string> GetLightTypeTextureName(Rendering::Settings::ELightType type)
	{
		using namespace Rendering::Settings;

		switch (type)
		{
		case ELightType::POINT: return "Point_Light";
		case ELightType::SPOT: return "Spot_Light";
		case ELightType::DIRECTIONAL: return "Directional_Light";
		case ELightType::AMBIENT_BOX: return "Ambient_Box_Light";
		case ELightType::AMBIENT_SPHERE: return "Ambient_Sphere_Light";
		}

		return std::nullopt;
	}

	Maths::FMatrix4 CreateDebugDirectionalLight()
	{
		Rendering::Entities::Light directionalLight;
		directionalLight.intensity = 2.0f;
		directionalLight.type = Rendering::Settings::ELightType::DIRECTIONAL;

		directionalLight.transform->SetLocalPosition({ 0.0f, 10.0f, 0.0f });
		directionalLight.transform->SetLocalRotation(Maths::FQuaternion({ 120.0f, -40.0f, 0.0f }));
		return directionalLight.GenerateMatrix();
	}

	Maths::FMatrix4 CreateDebugAmbientLight()
	{
		Rendering::Entities::Light light;
		light.intensity = 0.01f;
		light.constant = 10000.0f;
		light.type = Rendering::Settings::ELightType::AMBIENT_SPHERE;
		return light.GenerateMatrix();
	}

	std::unique_ptr<Rendering::HAL::ShaderStorageBuffer> CreateDebugLightBuffer()
	{
		auto lightBuffer = std::make_unique<Rendering::HAL::ShaderStorageBuffer>();
		Maths::FMatrix4 lightMatrices[2] = {
			CreateDebugDirectionalLight(),
			CreateDebugAmbientLight() };

		lightBuffer->Allocate(sizeof(Maths::FMatrix4) * 2, Rendering::Settings::EAccessSpecifier::STATIC_READ);
		lightBuffer->Upload(&lightMatrices[0]);

		return lightBuffer;
	}
}



Editor::Rendering::DebugSceneRenderer::DebugSceneRenderer(::Rendering::Context::Driver& p_driver) :
	::Core::Rendering::SceneRenderer(p_driver, true /* enable stencil write, required by the grid */)
{
	AddFeature<::Rendering::Features::FrameInfoRenderFeature, ::Rendering::Features::EFeatureExecutionPolicy::ALWAYS>();
	//AddFeature<::Rendering::Features::DebugShapeRenderFeature, ::Rendering::Features::EFeatureExecutionPolicy::FRAME_EVENTS_ONLY>();
	AddFeature<Editor::Rendering::DebugModelRenderFeature, ::Rendering::Features::EFeatureExecutionPolicy::NEVER>();
	//AddFeature<OutlineRenderFeature, ::Rendering::Features::EFeatureExecutionPolicy::NEVER>();

	AddPass<PointRenderPass>("PointDraw", ::Rendering::Settings::ERenderPassOrder::Opaque).SetEnabled(false);	
	AddPass<GridRenderPass>("Grid", ::Rendering::Settings::ERenderPassOrder::Opaque);
	AddPass<PathTraceRenderPass>("Path Tracing", ::Rendering::Settings::ERenderPassOrder::PathTrace).SetEnabled(false);
	AddPass<GizmoRenderPass>("ImRenderer", ::Rendering::Settings::ERenderPassOrder::Last);
	AddPass<PickingRenderPass>("Picking", ::Rendering::Settings::ERenderPassOrder::Last);
}
