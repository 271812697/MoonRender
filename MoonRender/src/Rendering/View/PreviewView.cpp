#include <Rendering/View/PreviewView.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <filesystem>
#include <vector>

#include <Core/ECS/Components/CDirectionalLight.h>
#include <Core/ECS/Components/CMaterialRenderer.h>
#include <Core/ECS/Components/CModelRenderer.h>
#include <Core/ECS/Components/CPostProcessStack.h>
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
#include <Rendering/Resources/Loaders/ModelLoader.h>
#include <Rendering/Resources/Mesh.h>
#include <Tools/Utils/PathParser.h>

namespace
{
	constexpr float kPi = 3.14159265358979323846f;
	/** The preview sphere has radius one and sits at the origin; the camera's default distance
	 * frames it, and the light rig is laid out against the camera's distance. */
	constexpr float kDefaultDistance = 4.6f;

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
				// Counter-clockwise seen from outside - the winding OpenGL treats as the front
				// face (the engine never calls glFrontFace, so it is the default CCW). The
				// previous order was clockwise, so back-face culling kept the *far* hemisphere
				// and the material shaded the back side of the sphere: its normals point away
				// from the camera (NoV == 0), the DFG term sat at the grazing-angle value and
				// the "reflection" was the environment sampled along n (matcap), not along
				// reflect(-v, n).
				p_indices.insert(p_indices.end(), { a, a + 1, b, a + 1, b + 1, b });
			}
		}
	}

	/** The cube: one quad per face, so each face keeps its own normal and tangent frame. The
	 * material's UVs are 0..1 per face, which is what a tiling/clamp check wants to see. */
	void BuildCube(
		std::vector<::Rendering::Geometry::Vertex>& p_vertices,
		std::vector<uint32_t>& p_indices)
	{
		struct Face
		{
			Maths::FVector3 normal;
			Maths::FVector3 tangent;
			Maths::FVector3 bitangent;
		};

		// tangent x bitangent == normal on every face keeps the counter-clockwise winding below
		// consistent with the sphere's.
		const Face faces[6] = {
			{ { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, { 0.0f, 1.0f, 0.0f } },
			{ { -1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f } },
			{ { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f } },
			{ { 0.0f, -1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
			{ { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
			{ { 0.0f, 0.0f, -1.0f }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
		};

		for (const Face& face : faces)
		{
			const uint32_t base = static_cast<uint32_t>(p_vertices.size());

			for (int corner = 0; corner < 4; ++corner)
			{
				const float u = (corner == 1 || corner == 2) ? 1.0f : 0.0f;
				const float v = (corner >= 2) ? 1.0f : 0.0f;
				const Maths::FVector3 position =
					face.normal + face.tangent * (u * 2.0f - 1.0f) + face.bitangent * (v * 2.0f - 1.0f);

				::Rendering::Geometry::Vertex vertex{};
				vertex.position[0] = position.x;
				vertex.position[1] = position.y;
				vertex.position[2] = position.z;
				vertex.texCoords[0] = u;
				vertex.texCoords[1] = v;
				vertex.normals[0] = face.normal.x;
				vertex.normals[1] = face.normal.y;
				vertex.normals[2] = face.normal.z;
				vertex.tangent[0] = face.tangent.x;
				vertex.tangent[1] = face.tangent.y;
				vertex.tangent[2] = face.tangent.z;
				vertex.bitangent[0] = face.bitangent.x;
				vertex.bitangent[1] = face.bitangent.y;
				vertex.bitangent[2] = face.bitangent.z;
				p_vertices.push_back(vertex);
			}

			p_indices.insert(p_indices.end(), {
				base, base + 1, base + 2, base, base + 2, base + 3 });
		}
	}

	/** The plane: a 1x1 quad in the XY plane, facing +Z (the camera's side), UV 0..1. */
	void BuildPlane(
		std::vector<::Rendering::Geometry::Vertex>& p_vertices,
		std::vector<uint32_t>& p_indices)
	{
		const Maths::FVector3 normal{ 0.0f, 0.0f, 1.0f };
		const Maths::FVector3 tangent{ 1.0f, 0.0f, 0.0f };
		const Maths::FVector3 bitangent{ 0.0f, 1.0f, 0.0f };

		for (int corner = 0; corner < 4; ++corner)
		{
			const float u = (corner == 1 || corner == 2) ? 1.0f : 0.0f;
			const float v = (corner >= 2) ? 1.0f : 0.0f;
			const Maths::FVector3 position = tangent * (u * 2.0f - 1.0f) + bitangent * (v * 2.0f - 1.0f);

			::Rendering::Geometry::Vertex vertex{};
			vertex.position[0] = position.x;
			vertex.position[1] = position.y;
			vertex.position[2] = position.z;
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

		p_indices.insert(p_indices.end(), { 0, 1, 2, 0, 2, 3 });
	}

	/** The "material ball" most engines show a material on: a UV sphere with a circular dimple
	 * facing +Z, resting on a small pedestal.
	 *
	 * The dimple is a radial modulation of the sphere, so the parametric direction - and with it
	 * the UVs and the tangent frame - stays the UV sphere's; the normal comes from the analytic
	 * normal of the modulated surface, which is what makes the rim read as a rim instead of a
	 * dented sphere. The pedestal is a surface of revolution. */
	void BuildMaterialBall(
		std::vector<::Rendering::Geometry::Vertex>& p_vertices,
		std::vector<uint32_t>& p_indices)
	{
		using namespace Maths;

		constexpr float pi = 3.14159265358979323846f;
		constexpr uint32_t rings = 32;
		constexpr uint32_t segments = 64;
		constexpr float dentAngle = 0.55f;  // angular radius of the dimple (~31.5 degrees)
		constexpr float dentDepth = 0.24f;
		constexpr float centerY = 0.1f;

		const auto addVertex = [&](const FVector3& p_position, const FVector3& p_normal,
			const FVector3& p_tangent, float p_u, float p_v)
		{
			const FVector3 bitangent = FVector3::Normalize(FVector3::Cross(p_normal, p_tangent));

			::Rendering::Geometry::Vertex vertex{};
			vertex.position[0] = p_position.x;
			vertex.position[1] = p_position.y;
			vertex.position[2] = p_position.z;
			vertex.texCoords[0] = p_u;
			vertex.texCoords[1] = p_v;
			vertex.normals[0] = p_normal.x;
			vertex.normals[1] = p_normal.y;
			vertex.normals[2] = p_normal.z;
			vertex.tangent[0] = p_tangent.x;
			vertex.tangent[1] = p_tangent.y;
			vertex.tangent[2] = p_tangent.z;
			vertex.bitangent[0] = bitangent.x;
			vertex.bitangent[1] = bitangent.y;
			vertex.bitangent[2] = bitangent.z;
			p_vertices.push_back(vertex);
		};

		const uint32_t stride = segments + 1;
		for (uint32_t ring = 0; ring <= rings; ++ring)
		{
			const float v = static_cast<float>(ring) / rings;
			const float phi = v * pi;
			const float sinPhi = std::sin(phi);
			const float cosPhi = std::cos(phi);

			for (uint32_t segment = 0; segment <= segments; ++segment)
			{
				const float u = static_cast<float>(segment) / segments;
				const float theta = u * 2.0f * pi;
				const float sinTheta = std::sin(theta);
				const float cosTheta = std::cos(theta);


				const FVector3 radial{ sinPhi * cosTheta, cosPhi, sinPhi * sinTheta };

				// The dimple faces +Z, the side the default camera looks from.
				const float alpha = std::acos(std::min(1.0f, std::max(-1.0f, radial.z)));
				float radius = 1.0f;
				float slope = 0.0f; // dR/dalpha
				if (alpha < dentAngle)
				{
					const float t = alpha / dentAngle;
					radius -= dentDepth * (0.5f + 0.5f * std::cos(pi * t));
					// dR/dalpha: the bump's derivative changes sign with the minus above, so the
					// slope is positive - the surface rises from the dimple's centre to its rim.
					slope = dentDepth * (0.5f * pi / dentAngle) * std::sin(pi * t);
				}

				// Surface normal of a radially modulated sphere: R * e - R' * de/dalpha.
				const FVector3 increasingAlpha{ cosPhi * cosTheta, -sinPhi, cosPhi * sinTheta };
				const FVector3 normal = alpha < 1.0e-4f
					? radial
					: FVector3::Normalize(radial * radius - increasingAlpha * slope);

				FVector3 tangent = sinPhi < 1.0e-4f
					? FVector3{ 1.0f, 0.0f, 0.0f }
					: FVector3{ -sinTheta, 0.0f, cosTheta };
				tangent = FVector3::Normalize(tangent - normal * FVector3::Dot(normal, tangent));

				addVertex(radial * radius + FVector3{ 0.0f, centerY, 0.0f }, normal, tangent, u, v);
			}
		}

		for (uint32_t ring = 0; ring < rings; ++ring)
		{
			for (uint32_t segment = 0; segment < segments; ++segment)
			{
				const uint32_t a = ring * stride + segment;
				const uint32_t b = a + stride;
				// Same counter-clockwise (outward) winding as BuildUVSphere.
				p_indices.insert(p_indices.end(), { a, a + 1, b, a + 1, b + 1, b });
			}
		}

		// The pedestal: a profile revolved around Y, top face down to bottom face. The sphere's
		// bottom point rests on the first profile point.
		struct ProfilePoint
		{
			float radius;
			float height;
		};
		static constexpr ProfilePoint profile[] = {
			{ 0.00f, -0.90f }, { 0.40f, -0.90f }, { 0.44f, -0.93f }, { 0.44f, -1.00f },
			{ 0.56f, -1.10f }, { 0.60f, -1.14f }, { 0.54f, -1.16f }, { 0.00f, -1.16f }
		};
		constexpr uint32_t baseRows = sizeof(profile) / sizeof(profile[0]);
		constexpr uint32_t baseSegments = 64;
		constexpr uint32_t baseStride = baseSegments + 1;
		const uint32_t baseVertexStart = static_cast<uint32_t>(p_vertices.size());

		for (uint32_t row = 0; row < baseRows; ++row)
		{
			const ProfilePoint& current = profile[row];
			const ProfilePoint& previous = profile[row > 0 ? row - 1 : row];
			const ProfilePoint& next = profile[row + 1 < baseRows ? row + 1 : row];

			// The 2D profile normal, rotated so it points away from the axis.
			float nr = -(next.height - previous.height);
			float ny = next.radius - previous.radius;
			const float length = std::sqrt(nr * nr + ny * ny);
			if (length > 1.0e-6f)
			{
				nr /= length;
				ny /= length;
			}

			for (uint32_t segment = 0; segment <= baseSegments; ++segment)
			{
				const float u = static_cast<float>(segment) / baseSegments;
				const float theta = u * 2.0f * pi;
				const float sinTheta = std::sin(theta);
				const float cosTheta = std::cos(theta);

				const FVector3 position{ current.radius * cosTheta, current.height, current.radius * sinTheta };
				const FVector3 normal = FVector3::Normalize(FVector3{ nr * cosTheta, ny, nr * sinTheta });
				const FVector3 tangent{ -sinTheta, 0.0f, cosTheta };

				addVertex(position, normal, tangent, u,
					static_cast<float>(row) / static_cast<float>(baseRows - 1));
			}
		}

		// Counter-clockwise seen from outside, like the sphere above: the geometric normal must
		// agree with the surface normal. The pedestal's flat top and bottom turn a corner
		// relative to its walls, so the order is picked per triangle.
		const auto addBaseTriangle = [&](uint32_t p_a, uint32_t p_b, uint32_t p_c)
		{
			const auto& a = p_vertices[p_a];
			const auto& b = p_vertices[p_b];
			const auto& c = p_vertices[p_c];
			const FVector3 positionA{ a.position[0], a.position[1], a.position[2] };
			const FVector3 positionB{ b.position[0], b.position[1], b.position[2] };
			const FVector3 positionC{ c.position[0], c.position[1], c.position[2] };
			const FVector3 normalA{ a.normals[0], a.normals[1], a.normals[2] };
			const FVector3 normalB{ b.normals[0], b.normals[1], b.normals[2] };
			const FVector3 normalC{ c.normals[0], c.normals[1], c.normals[2] };

			const FVector3 geometric = FVector3::Cross(positionB - positionA, positionC - positionA);
			if (FVector3::Dot(geometric, geometric) < 1.0e-12f)
			{
				return; // the pole rows collapse to a point
			}

			if (FVector3::Dot(geometric, FVector3::Normalize(normalA + normalB + normalC)) < 0.0f)
			{
				const uint32_t swap = p_b;
				p_b = p_c;
				p_c = swap;
			}
			p_indices.insert(p_indices.end(), { p_a, p_b, p_c });
		};

		for (uint32_t row = 0; row + 1 < baseRows; ++row)
		{
			for (uint32_t segment = 0; segment < baseSegments; ++segment)
			{
				const uint32_t a = baseVertexStart + row * baseStride + segment;
				const uint32_t b = a + baseStride;
				addBaseTriangle(a, b, a + 1);
				addBaseTriangle(a + 1, b, b + 1);
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

	// Distance fade is a scene-space effect (an LOD fade for far-away geometry). The preview
	// camera is close enough to sit inside its range, and the shader's dither then punches the
	// surface into a dot pattern; a preview shows the material, not the scene's distance.
	m_renderer->SetExcludedFeatures(::Rendering::Data::FeatureSet{ "DISTANCE_FADE" });

	// The passes a preview has nothing to feed, switched off so it does not pay for them:
	//  - the sky's environment capture (HDR load, cubemap capture, irradiance/prefilter
	//    convolution): a preview only wants the backdrop, and the scene itself does not bind
	//    _EnvironmentMap either (its reflection feature is off), so lighting stays the same.
	//  - no G-buffer/SSAO: nothing in this build registers the SSAO feature, so the buffers are
	//    rendered and never read.
	//  - no HZB: the culler needs a scene BVH, and a preview scene has no reason to build one.
	// What is left is the scene's own opaque (or transparent) path, plus its post-process.
	{
		auto& skyPass = m_renderer->GetPass<::Core::Rendering::SkyboxRenderPass>("SkyboxRenderPass");
		skyPass.SetEnabled(true);
		// Filament's preview lights the ball from a studio IBL, so the preview bakes the
		// studio HDR instead of reusing the scene's sky; the backdrop stays the neutral
		// gradient below.
		skyPass.SetEnvironmentCapture(true);
		skyPass.updateSkyTexture(":Textures/Studio.hdr");
		// The preview's IBL is calibrated against Qt-Filament's studio intensity. Filament
		// gives IndirectLight 20000 lux and uses the default camera exposure (1/125s, f/16,
		// ISO 100 -> 2.604e-5), so its shader-side iblLuminance is 20000 * 2.604e-5 = 0.521.
		// Moon multiplies the baked environment by the reflection block's brightness (0.2 in
		// EngineBufferRenderFeature) and then by the post-process exposure (1.2), so the bake
		// needs 0.521 / (0.2 * 1.2) = 2.17. The earlier 4.0 was calibrated against the
		// over-sharp prefilter, which made the ball read much darker than it does with the
		// correct mips; with those it was 1.8x too bright (measured 0.57 vs Filament's 0.315
		// in pre-ACES scene radiance).
		skyPass.SetEnvironmentIntensity(2.2f);

		// A neutral vignette behind the material: darker at the centre (where the mesh is) and
		// lighter towards the corners, so the silhouette reads without a background plane.
		::Core::Rendering::SkyBoxSetting& sky = skyPass.GetSetting();
		sky.mode = ::Core::Rendering::SkyMode::Gradient;
		// Filament's preview backdrop: a flat, very dark slate (0.055, 0.060, 0.070), so the
		// material is judged against the same surround.
		sky.bottomColor = { 0.055f, 0.060f, 0.070f, 1.0f };
		sky.topColor = { 0.055f, 0.060f, 0.070f, 1.0f };
		sky.clearColor = { 0.055f, 0.060f, 0.070f, 1.0f };
	}
	m_renderer->GetPass<::Core::Rendering::GbufferPass>("Gbuffer").SetEnabled(false);
	m_renderer->GetPass<::Core::Rendering::HzbBuildPass>("HZB").SetEnabled(false);

	BuildPreviewMesh();
	BuildScene();

	m_camera.SetFov(45.0f);
	m_camera.SetNear(0.1f);
	m_camera.SetFar(100.0f);
	// Filament's material preview orbits from (1.50, 0.82, 3.65) looking at the origin (a 3/4
	// view, slightly above the equator). Matching that pose makes the IBL highlights land in
	// the same place as its preview instead of differing by the camera's azimuth.
	const Maths::FVector3 previewEye{ 1.50f, 0.82f, 3.65f };
	m_camera.SetPosition(previewEye);
	m_camera.SetRotation(Maths::FQuaternion::LookAt(
		Maths::FVector3::Normalize(Maths::FVector3{ -previewEye.x, -previewEye.y, -previewEye.z }), Maths::FVector3{ 0.0f, 1.0f, 0.0f }));
	// The background the preview had before it became a scene; with no sky pass it is the clear
	// colour, and the post-process turns it into the grey the panel shows.
	m_camera.SetClearColor({ 0.032f, 0.032f, 0.034f });

	// The zoom step is proportional to the scene's extent, which is two units here - a wheel
	// notch would barely move the camera. Give the preview a step a preview wants.
	m_cameraController.SetZoomSpeed(6.0f);

	UpdateLightRig();
}

void Editor::Panels::PreviewView::BuildPreviewMesh()
{
	std::vector<::Rendering::Geometry::Vertex> vertices;
	std::vector<uint32_t> indices;

	std::unique_ptr<::Rendering::Resources::Model> model;

	switch (m_previewMesh)
	{
	case EPreviewMesh::BALL:
	{
		// Filament's material sphere (assets/models/material_sphere in the filament repo,
		// Apache-2.0). Loaded rather than approximated: the pedestal is what gives a material a
		// second, flatter surface to read against.
		const std::filesystem::path path =
			std::filesystem::path(Tools::Utils::PathParser::GetExeDirectory())
			/ "Moon" / "Data" / "Engine" / "Models" / "material_sphere.obj";
		model = std::unique_ptr<::Rendering::Resources::Model>(
			::Rendering::Resources::Loaders::ModelLoader::Create(path.string()));

		if (model == nullptr)
		{
			// The asset is copied next to the executable by the build; without it the preview
			// still works on the procedural approximation.
			BuildMaterialBall(vertices, indices);
		}
		break;
	}
	case EPreviewMesh::CUBE:
		BuildCube(vertices, indices);
		break;
	case EPreviewMesh::PLANE:
		BuildPlane(vertices, indices);
		break;
	case EPreviewMesh::SPHERE:
	default:
		BuildUVSphere(vertices, indices);
		break;
	}

	if (model == nullptr)
	{
		model = std::make_unique<::Rendering::Resources::Model>(":Preview/Mesh");
		model->AddMesh(new ::Rendering::Resources::Mesh(vertices, indices));
		model->computeBoxAndShpere();
	}

	// Swap the model in before releasing the old one: the renderer's drawables point at its
	// meshes, and the next parse is what moves them over.
	::Rendering::Resources::Model* previous = m_previewModel.release();
	m_previewModel = std::move(model);

	if (m_previewActor != nullptr)
	{
		// Normalize with the mesh's own bounds, not with numbers picked per mesh: a loaded asset
		// is authored at whatever scale and origin it happens to have (Filament's sphere is
		// ~1.35 units tall and not centred), and the preview's camera frames a 2-unit object.
		m_previewModel->computeBoxAndShpere();
		const ::Rendering::Geometry::bbox& bounds = m_previewModel->GetBoundingBox();
		const Maths::FVector3 size = bounds.pmax - bounds.pmin;
		const float extent = std::max({ size.x, size.y, size.z });
		const float scale = extent > 1.0e-4f ? 2.0f / extent : 1.0f;
		const Maths::FVector3 center = (bounds.pmin + bounds.pmax) * 0.5f;

		m_previewActor->transform.SetLocalScale({ scale, scale, scale });
		// Put the mesh's centre at the orbit centre, so orbiting turns around the object.
		m_previewActor->transform.SetLocalPosition(
			{ -center.x * scale, -center.y * scale, -center.z * scale });

		if (auto* renderer = m_previewActor->GetComponent<::Core::ECS::Components::CModelRenderer>())
		{
			renderer->SetModel(m_previewModel.get());
		}
	}

	delete previous;
}

void Editor::Panels::PreviewView::BuildScene()
{
	using namespace ::Core::ECS::Components;

	// The actor that shows the material. The model renderer goes in first: the material renderer
	// reads the model's material list while constructing.
	m_previewActor = &m_scene.CreateActor("Preview Target");
	m_previewActor->AddComponent<CModelRenderer>().SetModel(m_previewModel.get());
	m_previewActor->AddComponent<CMaterialRenderer>();

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

	// Filament's material preview is a sun plus the studio IBL, with no point fill: the key,
	// fill and rim lights stay in the scene (UpdateLightRig still places them) but contribute
	// nothing, so the two previews shade the same setup.
	constexpr float kKeyIntensity = 0.0f;
	m_keyLight = addPointLight("Preview Key Light", kKeyIntensity);
	m_fillLight = addPointLight("Preview Fill Light", kKeyIntensity);
	m_rimLightRight = addPointLight("Preview Rim Light Right", kKeyIntensity);
	m_rimLightLeft = addPointLight("Preview Rim Light Left", kKeyIntensity);
	m_headLight = addPointLight("Preview Head Light", kKeyIntensity);

	// The directional light every viewport scene starts with, so the default shading direction
	// matches the viewport's. It does not cast shadows here: the preview has no scene to receive
	// them, and its shadow map only produced self-shadowing acne on the ball (black speckles).
	m_scene.AddDefaultLights();
	if (auto* defaultLight = m_scene.FindActorByTag("DirectionalLight"))
	{
		if (auto* light = defaultLight->GetComponent<CDirectionalLight>())
		{
			// Filament's preview sun: 40000 lux against its 20000 IBL. Filament's camera
			// exposure is 2.604e-5 (1/125s, f/16, ISO 100), so the sun reaches the shader as
			// 40000 * 2.604e-5 = 1.042. Moon's tonemapper applies its own 1.2 exposure after
			// lighting, so the light itself carries 1.042 / 1.2 = 0.87; with the IBL at 2.2
			// this also restores Filament's 2:1 sun-to-environment ratio.
			light->SetIntensity(0.87f);
			light->SetColor({ 1.0f, 0.912f, 0.787f });
			light->SetCastShadows(false);
			defaultLight->transform.SetLocalRotation(Maths::FQuaternion::LookAt(
				{ -0.35f, -0.85f, -0.55f }, { 0.0f, 1.0f, 0.0f }));
		}
	}
	// Tone mapping, gamma correction and the rest of the stack: without it the frame would
	// leave the view linear, and the preview would not look like the viewport.
	m_scene.AddDefaultPostProcessStack();

	// ...but with a fixed tone curve, not the viewport's adaptive one. Bloom and auto exposure
	// react to what is being shown (a bright metal would drive the exposure and make every
	// material look the same), and the neutral tone curve clips a rough metal's environment to
	// flat white. A preview needs one constant studio exposure, so the material is judged by its
	// own values.
	if (auto* stackActor = m_scene.FindActorByTag("PostProcessStack"))
	{
		if (auto* stack = stackActor->GetComponent<CPostProcessStack>())
		{
			auto bloom = stack->GetBloomSettings();
			bloom.enabled = false;
			stack->SetBloomSettings(bloom);

			auto autoExposure = stack->GetAutoExposureSettings();
			autoExposure.enabled = false;
			stack->SetAutoExposureSettings(autoExposure);

			auto tonemapping = stack->GetTonemappingSettings();
			tonemapping.enabled = true;
			tonemapping.mode = ::Core::Rendering::PostProcess::ETonemappingMode::ACES;
			// _Exposure is a multiplier, not an EV. Keep the preview exposure fixed; IBL
			// brightness is matched on the environment side, not by lifting the backdrop.
			tonemapping.exposure = 1.2f;
			tonemapping.gammaCorrection = true;
			stack->SetTonemappingSettings(tonemapping);
		}
	}
}

bool Editor::Panels::PreviewView::IsCameraMoving() const
{
	return m_cameraController.HasPendingMovement();
}

void Editor::Panels::PreviewView::ResetCameraTransform()
{
	// Clear stale destinations / mouse state first: a preview that lost a mouse-release
	// event would otherwise drop every new destination on the next frame.
	m_cameraController.ClearMovement();
	Maths::FVector3 eye{ 1.50f, 0.82f, 3.65f };
	// MOON_PREVIEW_CAM_ELEV=<degrees>: swing the preview camera around X, so a render can be
	// captured from below the equator without the UI (a probe for "it goes wrong from below").
	if (const char* p_elevation = std::getenv("MOON_PREVIEW_CAM_ELEV"); p_elevation != nullptr)
	{
		const float radians = std::strtof(p_elevation, nullptr) * 3.14159265358979f / 180.0f;
		const float distance = Maths::FVector3::Length(eye);
		eye = Maths::FVector3{
			eye.x,
			eye.y * std::cos(radians) - eye.z * std::sin(radians),
			eye.y * std::sin(radians) + eye.z * std::cos(radians) };
		eye = Maths::FVector3::Normalize(eye) * distance;
	}
	m_cameraController.MoveToPose(eye, Maths::FQuaternion::LookAt(
		Maths::FVector3::Normalize(Maths::FVector3{ -eye.x, -eye.y, -eye.z }),
		Maths::FVector3{ 0.0f, 1.0f, 0.0f }));
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

	auto* materials = m_previewActor != nullptr
		? m_previewActor->GetComponent<::Core::ECS::Components::CMaterialRenderer>()
		: nullptr;
	if (materials == nullptr)
	{
		return;
	}

	if (p_material != nullptr)
	{
		// Fill every slot: a loaded model (the material sphere) may split its geometry by
		// material, and the preview shows the same material on all of it.
		materials->FillWithMaterial(*p_material);
	}
	else
	{
		materials->RemoveAllMaterials();
	}
}

void Editor::Panels::PreviewView::SetPreviewMesh(EPreviewMesh p_mesh)
{
	if (p_mesh == m_previewMesh)
	{
		return;
	}

	m_previewMesh = p_mesh;
	BuildPreviewMesh();
}

Editor::Panels::EPreviewMesh Editor::Panels::PreviewView::GetPreviewMesh() const
{
	return m_previewMesh;
}

::Core::Resources::Material* Editor::Panels::PreviewView::GetMaterial() const
{
	return m_material;
}

::Core::SceneSystem::Scene* Editor::Panels::PreviewView::GetScene()
{
	return &m_scene;
}
