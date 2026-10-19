#include <tracy/Tracy.hpp>
#include <Core/ECS/Components/CMaterialRenderer.h>
#include <Core/Rendering/EngineDrawableDescriptor.h>
#include <Core/Rendering/ReflectionRenderFeature.h>
#include <Rendering/Features/LightingRenderFeature.h>

namespace
{
	bool IsAffectedByReflectionProbe(
		const Maths::FMatrix4& p_modelMatrix,
		const Rendering::Geometry::BoundingSphere& p_bounds,
		const Core::ECS::Components::CReflectionProbe& p_probe
	)
	{
		if (p_probe.GetInfluencePolicy() == Core::ECS::Components::CReflectionProbe::EInfluencePolicy::GLOBAL)
		{
			return true;
		}

		// Transform the bounding sphere to world space using the model matrix
		const auto worldSphereCenter = p_modelMatrix * Maths::FVector4(p_bounds.position, 1.0f);

		// Calculate the world space radius by applying the model matrix scale
		// Extract the scale from the model matrix (assuming uniform scaling for simplicity)
		const auto scaleX = Maths::FVector3::Length({ p_modelMatrix.data[0], p_modelMatrix.data[1], p_modelMatrix.data[2] });
		const auto scaleY = Maths::FVector3::Length({ p_modelMatrix.data[4], p_modelMatrix.data[5], p_modelMatrix.data[6] });
		const auto scaleZ = Maths::FVector3::Length({ p_modelMatrix.data[8], p_modelMatrix.data[9], p_modelMatrix.data[10] });
		const auto maxScale = std::max({ scaleX, scaleY, scaleZ });
		const auto worldSphereRadius = p_bounds.radius * maxScale;

		// Get the probe's transform to construct the OBB
		const auto& probeTransform = p_probe.owner.transform;
		// Get the probe's influence position
		const auto& probeInfluencePosition =
			probeTransform.GetWorldPosition() +
			p_probe.GetCapturePosition();
		// Get the rotation matrix from the probe's transform
		const auto probeRotation = probeTransform.GetWorldRotation();

		// Convert sphere center to probe's local space
		const auto sphereCenterLocal = Maths::FVector3{
			worldSphereCenter.x,
			worldSphereCenter.y,
			worldSphereCenter.z
		} - probeInfluencePosition;

		// Rotate the sphere center into the OBB's local coordinate system
		const auto rotationMatrix = Maths::FMatrix4::Rotation(probeRotation);
		const auto sphereCenterOBBLocal = Maths::FMatrix4::Transpose(rotationMatrix) * sphereCenterLocal;

		// Half extents of the OBB (the size is already expressed as half extents)
		const auto halfExtents = p_probe.GetInfluenceSize();

		// Find the closest point on the AABB (in OBB local space) to the sphere center
		auto closestPoint = sphereCenterOBBLocal;

		// Clamp to AABB bounds
		closestPoint.x = std::max(-halfExtents.x, std::min(halfExtents.x, closestPoint.x));
		closestPoint.y = std::max(-halfExtents.y, std::min(halfExtents.y, closestPoint.y));
		closestPoint.z = std::max(-halfExtents.z, std::min(halfExtents.z, closestPoint.z));

		// Calculate distance squared from sphere center to closest point on OBB
		const auto diff = sphereCenterOBBLocal - closestPoint;
		const auto distanceSquared = diff.x * diff.x + diff.y * diff.y + diff.z * diff.z;

		// Check if distance is less than sphere radius
		return distanceSquared <= (worldSphereRadius * worldSphereRadius);
	}

	// The best suited probe will always be local er global, and when multiple probes
	// are found, the closest one to the drawable will be selected.
	Tools::Utils::OptRef<const Core::ECS::Components::CReflectionProbe> FindBestReflectionProbe(
		const Rendering::Entities::Drawable& p_drawable,
		const Core::Rendering::EngineDrawableDescriptor& p_engineDrawableDesc,
		std::span<const std::reference_wrapper<Core::ECS::Components::CReflectionProbe>> p_probes)
	{
		// Stores the best local and global probes found so far.
		struct {
			Tools::Utils::OptRef<const Core::ECS::Components::CReflectionProbe> probe;
			float distance = std::numeric_limits<float>::max();
		} bestLocal, bestGlobal;

		// Extract the drawable's world position from the model matrix
		const auto& modelMatrix = p_engineDrawableDesc.modelMatrix;
		const auto drawableWorldPosition = Maths::FVector3{
			modelMatrix.data[3], // Translation X
			modelMatrix.data[7], // Translation Y
			modelMatrix.data[11]  // Translation Z
		};

		// Process all probes to find influencing ones
		for (auto& probeRef : p_probes)
		{
			const auto& probe = probeRef.get();

			const auto& probeTransform = probe.owner.transform;
			const auto probeWorldPosition = probeTransform.GetWorldPosition() + probe.GetCapturePosition();
			const auto distance = Maths::FVector3::Distance(drawableWorldPosition, probeWorldPosition);

			const bool isLocal = probe.GetInfluencePolicy() == Core::ECS::Components::CReflectionProbe::EInfluencePolicy::LOCAL;
			const bool isGlobal = !isLocal;

			// If we already have a valid local probe, we can safely skip further global probes.
			if (isGlobal && bestLocal.probe.has_value())
			{
				continue;
			}

			auto& currentBest = isLocal ? bestLocal : bestGlobal;

			// Skip this probe if it's further than the best found so far.
			if (distance > currentBest.distance)
			{
				continue;
			}

			// Skip probes that don't affect this drawable
			if(!IsAffectedByReflectionProbe(
				p_engineDrawableDesc.modelMatrix,
				p_drawable.mesh->GetBoundingSphere(),
				probe))
			{
				continue;
			}

			// If we made it here, this probe is a candidate for the best probe.
			currentBest = { probe, distance };
		}

		return bestLocal.probe ? bestLocal.probe : bestGlobal.probe;
	}
}

Core::Rendering::ReflectionRenderFeature::ReflectionRenderFeature(
	::Rendering::Core::CompositeRenderer& p_renderer,
	::Rendering::Features::EFeatureExecutionPolicy p_executionPolicy
) :
	ARenderFeature(p_renderer, p_executionPolicy)
{
	m_defaultReflectionUbo = std::make_unique<::Rendering::HAL::UniformBuffer>();

	// Neutral probe data used when the scene has no reflection probe:
	// brightness 1, infinite box, not local, so image-based lighting still
	// evaluates with the bound environment map.
#pragma pack(push, 1)
	struct
	{
		Maths::FVector4 position;
		Maths::FMatrix4 rotation;
		Maths::FVector4 boxCenter;
		Maths::FVector4 boxHalfExtents;
		float brightness;
		bool boxProjection;
		std::byte padding1[3];
		bool local;
		std::byte padding2[3];
	} defaultData{
		.position = Maths::FVector4(0.0f, 0.0f, 0.0f, 0.0f),
		.rotation = Maths::FMatrix4::Identity,
		.boxCenter = Maths::FVector4(0.0f, 0.0f, 0.0f, 0.0f),
		.boxHalfExtents = Maths::FVector4(1e6f, 1e6f, 1e6f, 0.0f),
		.brightness = 1.0f,
		.boxProjection = false,
		.local = false
	};
#pragma pack(pop)

	m_defaultReflectionUbo->Allocate(sizeof(defaultData));
	m_defaultReflectionUbo->Upload(&defaultData);
}

void Core::Rendering::ReflectionRenderFeature::PrepareProbe(Core::ECS::Components::CReflectionProbe& p_reflectionProbe)
{
	p_reflectionProbe._PrepareUBO();
}

void Core::Rendering::ReflectionRenderFeature::SendProbeData(
	::Rendering::Data::Material& p_material,
	Tools::Utils::OptRef<const Core::ECS::Components::CReflectionProbe> p_reflectionProbe
)
{
	p_material.SetProperty(
		"_EnvironmentMap",
		p_reflectionProbe.has_value() ? p_reflectionProbe->GetCubemap().get() : static_cast<::Rendering::HAL::TextureHandle*>(nullptr),
		true
	);
}

void Core::Rendering::ReflectionRenderFeature::BindProbe(const Core::ECS::Components::CReflectionProbe& p_reflectionProbe)
{
	p_reflectionProbe._GetUniformBuffer().Bind(1);
}

void Core::Rendering::ReflectionRenderFeature::OnBeginFrame(const ::Rendering::Data::FrameDescriptor& p_frameDescriptor)
{
	assert(
		m_renderer.HasDescriptor<ReflectionRenderFeature::ReflectionDescriptor>()&&
		"Cannot find ReflectionDescriptor attached to this renderer"
	);

	const auto& reflectionDescriptor = m_renderer.GetDescriptor<ReflectionRenderFeature::ReflectionDescriptor>();

	for (auto& probe : reflectionDescriptor.reflectionProbes)
	{
		PrepareProbe(probe.get());
	}
}

void Core::Rendering::ReflectionRenderFeature::OnBeforeDraw(::Rendering::Data::PipelineState& p_pso, const ::Rendering::Entities::Drawable& p_drawable)
{
	ZoneScoped;

	auto& material = p_drawable.material.value();

	// Skip materials that aren't properly set to receive reflections.
	if (!material.IsReflectionReceiver() || !material.HasProperty("_EnvironmentMap"))
	{
		return;
	}

	assert(m_renderer.HasDescriptor<ReflectionRenderFeature::ReflectionDescriptor>()&& "Cannot find ReflectionDescriptor attached to this renderer");
	assert(p_drawable.HasDescriptor<Core::Rendering::EngineDrawableDescriptor>()&&"Cannot find EngineDrawableDescriptor attached to this drawable");

	const auto& reflectionDescriptor = m_renderer.GetDescriptor<ReflectionRenderFeature::ReflectionDescriptor>();
	const auto& engineDrawableDesc = p_drawable.GetDescriptor<Core::Rendering::EngineDrawableDescriptor>();

	// Find the probe that is best suited for this drawable.
	auto targetProbe = FindBestReflectionProbe(
		p_drawable,
		engineDrawableDesc,
		reflectionDescriptor.reflectionProbes
	);

	SendProbeData(material, targetProbe);

	if (targetProbe)
	{
		BindProbe(targetProbe.value());
	}
	else
	{
		// No reflection probe in the scene: fall back to the skybox prefilter
		// cube with a neutral probe UBO, so image-based lighting keeps working
		// for materials like Standard.mat.
		material.SetProperty("_EnvironmentMap", m_renderer.GetPrefilterCube(), true);
		m_defaultReflectionUbo->Bind(1);
	}
}
