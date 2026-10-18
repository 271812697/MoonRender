#include <Rendering/View/PreviewView.h>

#include <cfloat>
#include <cmath>
#include <vector>

#include <Core/ECS/Components/CMaterialRenderer.h>
#include <Core/ECS/Components/CModelRenderer.h>
#include <Core/ECS/Components/CPointLight.h>
#include <Core/Rendering/GbufferPass.h>
#include <Core/Rendering/HzbBuildPass.h>
#include <Core/Rendering/SceneRenderer.h>
#include <Core/Rendering/SkyBoxRenderPass .h>
#include <Core/Resources/Material.h>
#include <Maths/FMatrix4.h>
#include <Maths/FVector3.h>
#include <Rendering/Context/Driver.h>
#include <Rendering/Geometry/Vertex.h>
#include <Rendering/Resources/Mesh.h>

namespace
{
	constexpr float kPi = 3.14159265358979323846f;
	/** The preview sphere has radius one and sits at the origin; the camera's default distance
	 * frames it, and the light rig is laid out against the camera's distance. */
	constexpr float kDefaultDistance = 3.2f;

	/** The sphere the material is shown on: a UV sphere of radius one at the origin.
	 *
	 * It is built here rather than loaded: a preview must not depend on a model file being
	 * present, and the camera, the light rig and the material's UVs all assume this exact
	 * sphere. Normals are the positions (radius one), and the tangent frame is derived from
	 * the UVs so a normal map on the material has the basis it expects. */
	void BuildUVSphere(
		std::vector<::Rendering::Geometry::Vertex>& p_vertices,
		std::vector<uint32_t>& p_indices,
		uint32_t p_rings = 24, uint32_t p_segments = 48)
	{
		p_vertices.reserve(static_cast<size_t>(p_rings + 1) * (p_segments + 1));
		p_indices.reserve(static_cast<size_t>(p_rings) * p_segments * 6);

		for (uint32_t ring = 0; ring <= p_rings; ++ring)
		{
			const float v = static_cast<float>(ring) / static_cast<float>(p_rings);
			const float phi = v * kPi;
			const float sinPhi = std::sin(phi);
			const float cosPhi = std::cos(phi);

			for (uint32_t segment = 0; segment <= p_segments; ++segment)
			{
				const float u = static_cast<float>(segment) / static_cast<float>(p_segments);
				const float theta = u * 2.0f * kPi;

				const Maths::FVector3 normal{
					sinPhi * std::cos(theta),
					cosPhi,
					sinPhi * std::sin(theta) };

				// dP/du is the tangent. It vanishes at the poles, where any direction
				// perpendicular to the normal is as good a tangent as another.
				const Maths::FVector3 tangent = sinPhi < 1.0e-4f
					? Maths::FVector3{ 1.0f, 0.0f, 0.0f }
					: Maths::FVector3{ -std::sin(theta), 0.0f, std::cos(theta) };
				const Maths::FVector3 bitangent = Maths::FVector3::Normalize(
					Maths::FVector3::Cross(normal, tangent));

				::Rendering::Geometry::Vertex vertex{};
				vertex.position[0] = normal.x;
				vertex.position[1] = normal.y;
				vertex.position[2] = normal.z;
				vertex.texCoords[0] = u;
				vertex.texCoords[1] = v;
				vertex.normals[0] = normal.x;
				vertex.normals[1] = normal.y;
				vertex.normals[2] = normal.z;
				vertex.tangent[0] = tangent.x;
				vertex.tangent[1] = tangent.y;
				vertex.tangent[2] = tangent.z;
				vertex.bitangent[0] = bitangent.x;
				vertex.bitangent[1] = bitangent.y;
				vertex.bitangent[2] = bitangent.z;
				p_vertices.push_back(vertex);
			}
		}

		const uint32_t stride = p_segments + 1;
		for (uint32_t ring = 0; ring < p_rings; ++ring)
		{
			for (uint32_t segment = 0; segment < p_segments; ++segment)
			{
				const uint32_t a = ring * stride + segment;
				const uint32_t b = a + stride;
				// The winding the engine's models use: counter-clockwise seen from outside,
				// so the material's default back-face culling keeps the sphere.
				p_indices.insert(p_indices.end(), { a, b, a + 1, a + 1, b, b + 1 });
			}
		}
	}
}

Editor::Panels::PreviewView::PreviewView(::Rendering::Context::Driver& p_driver) :
	AViewControllable("PreviewView", p_driver)
{
	// The scene view's renderer, not a preview one: the material is drawn by the code that draws
	// it in the scene. A difference on screen is then the material's, and not a second renderer's.
	m_renderer = std::make_unique<::Core::Rendering::SceneRenderer>(p_driver);

	// The passes a preview has nothing to feed, switched off so it does not pay for them:
	//  - no sky: there is no environment to show, and the pass would load an HDR, capture a
	//    cubemap and convolve it - a lot of GPU work and VRAM for a background. The view clears
	//    to its own colour instead.
	//  - no G-buffer/SSAO: nothing in this build registers the SSAO feature, so the buffers are
	//    rendered and never read.
	//  - no HZB: the culler needs a scene BVH, and a preview scene has no reason to build one.
	// What is left is the scene's own opaque (or transparent) path, plus its post-process.
	m_renderer->GetPass<::Core::Rendering::SkyboxRenderPass>("SkyboxRenderPass").SetEnabled(false);
	m_renderer->GetPass<::Core::Rendering::GbufferPass>("Gbuffer").SetEnabled(false);
	m_renderer->GetPass<::Core::Rendering::HzbBuildPass>("HZB").SetEnabled(false);

	BuildSphere();
	BuildScene();

	m_camera.SetFov(45.0f);
	m_camera.SetNear(0.1f);
	m_camera.SetFar(100.0f);
	// On +Z looking back at the origin: the sphere is the only thing in the scene and the camera
	// is the point of view onto it. The controller orbits this pose around the roater centre,
	// which stays at the origin.
	m_camera.SetPosition({ 0.0f, 0.0f, kDefaultDistance });
	m_camera.SetRotation(Maths::FQuaternion({ 0.0f, 180.0f, 0.0f }));
	// The background the preview had before it became a scene; with no sky pass it is the clear
	// colour, and the post-process turns it into the grey the panel shows.
	m_camera.SetClearColor({ 0.09f, 0.09f, 0.11f });

	UpdateLightRig();
}

void Editor::Panels::PreviewView::BuildSphere()
{
	std::vector<::Rendering::Geometry::Vertex> vertices;
	std::vector<uint32_t> indices;
	BuildUVSphere(vertices, indices);

	m_sphere = std::make_unique<::Rendering::Resources::Model>(":Preview/Sphere");
	m_sphere->AddMesh(new ::Rendering::Resources::Mesh(vertices, indices));
	m_sphere->computeBoxAndShpere();
}

void Editor::Panels::PreviewView::BuildScene()
{
	using namespace ::Core::ECS::Components;

	// The actor that shows the material. The model renderer goes in first: the material renderer
	// reads the model's material list while constructing.
	m_sphereActor = &m_scene.CreateActor("Preview Sphere");
	m_sphereActor->AddComponent<CModelRenderer>().SetModel(m_sphere.get());
	m_sphereActor->AddComponent<CMaterialRenderer>();

	// The rig the scene view builds around its focus (see SceneView::Update): a key, a fill and
	// two rims, plus a head light on the camera. Only the intensities live here - the positions
	// are recomputed around the camera in UpdateLightRig.
	auto addPointLight = [this](const char* p_name, float p_intensity)
	{
		auto& actor = m_scene.CreateActor(p_name);
		auto& light = actor.AddComponent<CPointLight>();
		light.SetIntensity(p_intensity);
		// No attenuation: the rig should light the sphere from a direction. With the distance
		// falloff on, zooming - which moves the camera and so the rig - would change how bright
		// the material looks.
		light.SetConstant(1.0f);
		light.SetQuadratic(0.0f);
		return &actor;
	};

	constexpr float kKeyIntensity = 0.25f;
	m_keyLight = addPointLight("Preview Key Light", kKeyIntensity);
	m_fillLight = addPointLight("Preview Fill Light", kKeyIntensity / 1.5f);
	m_rimLightRight = addPointLight("Preview Rim Light Right", kKeyIntensity / 3.5f);
	m_rimLightLeft = addPointLight("Preview Rim Light Left", kKeyIntensity / 3.5f);
	m_headLight = addPointLight("Preview Head Light", kKeyIntensity / 1.5f * 5.0f);

	// The directional light every viewport scene starts with, so the default shading direction
	// matches the viewport's.
	m_scene.AddDefaultLights();
	// Tone mapping, gamma correction and the rest of the stack: without it the frame would
	// leave the view linear, and the preview would not look like the viewport.
	m_scene.AddDefaultPostProcessStack();
}

void Editor::Panels::PreviewView::Update(float p_deltaTime)
{
	AViewControllable::Update(p_deltaTime);
	UpdateLightRig();
}

void Editor::Panels::PreviewView::UpdateLightRig()
{
	const Maths::FVector3 cameraPosition = m_camera.GetPosition();
	const float distance = Maths::FVector3::Length(cameraPosition);
	if (!std::isfinite(distance) || distance <= 1.0e-4f)
	{
		return;
	}

	// The frame the scene view lays its rig out in: +Z points back at the camera, +Y is up.
	// Its camera-space-to-world basis is what puts a compass point of that frame around the
	// sphere; the translation is dropped because the rig belongs to the target, not the eye.
	const Maths::FVector3 backToCamera = Maths::FVector3::Normalize(cameraPosition);
	const float angle = Maths::FVector3::AngleBetween(backToCamera, Maths::FVector3::Up);
	// Looking straight up or down leaves no up to build the frame from (see SceneView::Update).
	const Maths::FVector3 worldUp =
		(angle < FLT_EPSILON || std::abs(angle - kPi) < FLT_EPSILON)
		? Maths::FVector3{ 1.0f, 0.0f, 0.0f }
		: Maths::FVector3{ 0.0f, 1.0f, 0.0f };

	const Maths::FMatrix4 cameraToWorld = Maths::FMatrix4::Inverse(
		Maths::FMatrix4::CreateCameraView(cameraPosition, Maths::FVector3::Zero, worldUp));

	auto place = [&](::Core::ECS::Actor* p_light, float p_elevationDegrees, float p_azimuthDegrees)
	{
		if (p_light == nullptr)
		{
			return;
		}

		const float elevation = p_elevationDegrees * kPi / 180.0f;
		const float azimuth = p_azimuthDegrees * kPi / 180.0f;
		const Maths::FVector3 direction{
			std::cos(elevation) * std::sin(azimuth),
			std::sin(elevation),
			std::cos(elevation) * std::cos(azimuth) };

		const Maths::FVector3 world = Maths::FMatrix4::MulPoint(cameraToWorld, direction * distance);
		p_light->transform.SetWorldPosition(world - cameraPosition);
	};

	// The scene view's compass: key above and slightly to the side, fill from below, two rims
	// behind the sphere, and the head light on the camera itself.
	place(m_keyLight, 50.0f, 10.0f);
	place(m_fillLight, -75.0f, 10.0f);
	place(m_rimLightRight, 0.0f, 110.0f);
	place(m_rimLightLeft, 0.0f, -110.0f);
	if (m_headLight != nullptr)
	{
		m_headLight->transform.SetWorldPosition(cameraPosition);
	}
}

void Editor::Panels::PreviewView::SetMaterial(::Core::Resources::Material* p_material)
{
	m_material = p_material;

	auto* materials = m_sphereActor != nullptr
		? m_sphereActor->GetComponent<::Core::ECS::Components::CMaterialRenderer>()
		: nullptr;
	if (materials == nullptr)
	{
		return;
	}

	if (p_material != nullptr)
	{
		materials->SetMaterialAtIndex(0, *p_material);
	}
	else
	{
		materials->RemoveMaterialAtIndex(0);
	}
}

::Core::Resources::Material* Editor::Panels::PreviewView::GetMaterial() const
{
	return m_material;
}

::Core::SceneSystem::Scene* Editor::Panels::PreviewView::GetScene()
{
	return &m_scene;
}
