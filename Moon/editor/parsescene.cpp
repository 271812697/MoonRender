#include "parsescene.h"
#include "renderer/Context.h"
#include "editor/View/sceneview/viewerwidget.h"
#include "renderer/SceneView.h"
#include "Core/Global/ServiceLocator.h"
#include "Core/ECS/Components/CMaterialRenderer.h"
#include "Core/ECS/Components/CPointLight.h"
#include "Core/ECS/Components/CDirectionalLight.h"
#include "Core/ECS/Components/CAmbientSphereLight.h"
#include "Core/ECS/Components/CPostProcessStack.h"
#include "Settings/DebugSetting.h"
#include "io/io_occ_step.h"
#include "core/log.h"
#include "Core/Global/ServiceLocator.h"
#include "Core/ECS/Components/CMaterialRenderer.h"
#include <Core/ResourceManagement/MaterialManager.h>
#include <Core/ResourceManagement/ModelManager.h>
#include <Core/ResourceManagement/ShaderManager.h>

#include <Core/ResourceManagement/TextureManager.h>
#include <Core/SceneSystem/SceneManager.h>
#include <tinygltf/include/tiny_gltf.h>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/matrix4x4.h>
#include <assimp/postprocess.h>

namespace MOON {
	Maths::FVector3 GetSpherePosition(float a, float b, float radius) {
		float elevation = a / 180.0 * 3.14159265;
		float azimuth = b / 180.0 * 3.14159265;
		return Maths::FVector3(cos(elevation) * sin(azimuth), sin(elevation), cos(elevation) * cos(azimuth)) * radius;
	}
	void addSphereLight(Core::SceneSystem::Scene* scene) {
		DebugSettings::instance().addCallBack("showLight", "default", [scene](NodeBase* self) {
			bool value = self->getData<bool>();
			scene->FindActorByName("PointLight1")->SetActive(value);
			scene->FindActorByName("PointLight2")->SetActive(value);
			scene->FindActorByName("PointLight3")->SetActive(value);
			scene->FindActorByName("PointLight4")->SetActive(value);
			});
		
		auto& ac1 = scene->CreateActor("PointLight1");
		ac1.SetTag("PointLight");
		auto& pointLight1 = ac1.AddComponent<Core::ECS::Components::CPointLight>();
		// The camera carries these lights with it (see SceneView::Update), so they are
		// what keeps what is on screen lit from whatever direction it is looked at. They
		// have to out-weigh the sky the material reflects, or the shading stays pinned to
		// the world instead of turning with the camera.
		float kI = 0.25;
		float kB = kI / 1.5;
		float kC = kI / 3.5;

		pointLight1.SetIntensity(kI);
		pointLight1.SetConstant(1.0);
		//pointLight.SetLinear(0.0);

		pointLight1.SetQuadratic(0.0);

		//ac1.SetParent(*ambient);
		//ac1.transform.SetLocalPosition(GetSpherePosition(50, 10, 999));

		auto& ac2 = scene->CreateActor("PointLight2");
		ac2.SetTag("PointLight");
		auto& pointLight2 = ac2.AddComponent<Core::ECS::Components::CPointLight>();
		pointLight2.SetIntensity(kB);
		pointLight2.SetConstant(1.0);
		//pointLight.SetLinear(0.0);

		pointLight2.SetQuadratic(0.0);

		//ac2.SetParent(*ambient);
		//ac2.transform.SetLocalPosition(GetSpherePosition(-75, 10, 999));

		auto& ac3 = scene->CreateActor("PointLight3");
		ac3.SetTag("PointLight");
		auto& pointLight3 = ac3.AddComponent<Core::ECS::Components::CPointLight>();
		pointLight3.SetIntensity(kC);
		pointLight3.SetConstant(1.0);
		//pointLight.SetLinear(0.0);

		pointLight3.SetQuadratic(0.0);

		//ac3.SetParent(*ambient);
		//ac3.transform.SetLocalPosition(GetSpherePosition(0, 110, 999));

		auto& ac4 = scene->CreateActor("PointLight4");
		ac4.SetTag("PointLight");
		auto& pointLight4 = ac4.AddComponent<Core::ECS::Components::CPointLight>();

		pointLight4.SetIntensity(kC);
		pointLight4.SetConstant(1.0);
		//pointLight.SetLinear(0.0);

		pointLight4.SetQuadratic(0.0);

		//ac4.SetParent(*ambient);
		//ac4.transform.SetLocalPosition(GetSpherePosition(0, -110, 999));

		auto& ac5 = scene->CreateActor("HeadLight");
		ac5.SetTag("PointLight");
		auto& pointLight5 = ac5.AddComponent<Core::ECS::Components::CPointLight>();
		pointLight5.SetIntensity(kB*5);
		pointLight5.SetConstant(1.0);
		//pointLight.SetLinear(0.0);

		pointLight5.SetQuadratic(0.0);
		GetViewerWidget.addActorToTreeView(&ac1);
		GetViewerWidget.addActorToTreeView(&ac2);
		GetViewerWidget.addActorToTreeView(&ac3);
		GetViewerWidget.addActorToTreeView(&ac4);
		GetViewerWidget.addActorToTreeView(&ac5);
	}


	

	bool LoadSceneFromFile(const std::string& filename, Core::SceneSystem::Scene* scene);
	bool LoadGLTF(const std::string& filename, Core::SceneSystem::Scene* scene, bool binary);
	void ParseScene::ParseFile(const std::string& path) {
		Core::SceneSystem::Scene* scene = GetService(Editor::Core::Context).sceneManager.GetCurrentScene();
		if (scene == nullptr) {
			return;
		}
		static bool addLightFlag = false;
		if (!addLightFlag) {
			addLightFlag = true;
			GetService(Editor::Core::Context).sceneManager.LoadDefaultScene();
			scene = GetService(Editor::Core::Context).sceneManager.GetCurrentScene();
			scene->FindActorByName("Directional Light")->GetComponent<Core::ECS::Components::CDirectionalLight>()->SetIntensity(1.0f);
		    scene->FindActorByName("Directional Light")->GetComponent<Core::ECS::Components::CDirectionalLight>()->GetData().castShadows = true;
			addSphereLight(scene);
			GetViewerWidget.addActorToTreeView(scene->FindActorByName("Post Process Stack"));
		}

		std::string sceneName = path;
		std::string ext = sceneName.substr(sceneName.find_last_of(".") + 1);
		
/*		if (ext == "scene")
			LoadSceneFromFile(sceneName, scene);
		else */if (ext == "gltf")
			LoadGLTF(sceneName, scene, false);
		else if (ext == "glb")
			LoadGLTF(sceneName, scene, true);
		else if (ext == "STEP"|| ext== "stp" || ext == "step") {
			IO::ReadSTEP(sceneName.c_str(), scene);
		}
		else if (ext == "bin") {
			IO::ReadSTEPBin(sceneName.c_str(), scene);
		}
		else
		{
			
			auto model = GetService(Core::ResourceManagement::ModelManager).LoadResource(sceneName);
			if (model) {
				Core::Resources::Material* tempMat =GetService(Core::ResourceManagement::MaterialManager).GetResource(sceneName);
				if (tempMat == nullptr) {
					tempMat = new Core::Resources::Material();
					GetService(Core::ResourceManagement::MaterialManager).RegisterResource(sceneName, tempMat);

					tempMat->SetShader(Core::Global::ServiceLocator::Get<Editor::Core::Context>().shaderManager[":Shaders\\Standard.ovfx"]);
					// The shader's material block is the default state now, so the instance's
					// overrides have to come after it.
					tempMat->SetBackfaceCulling(false);
					tempMat->SetCastShadows(false);
					tempMat->SetReceiveShadows(false);
					// Enable the clip plane discard in Standard.ovfx.
					tempMat->AddFeature("CLIP_PLANE");
					// IBL: the three pieces Filament's standard model reads - irradiance
					// (diffuse), prefiltered radiance (specular) and the DFG lookup table.
					tempMat->SetProperty("_IrradianceCube", GetSceneView.GetRenderer().GetIrradianceCube());
					tempMat->SetProperty("_PrefilterCube", GetSceneView.GetRenderer().GetPrefilterCube());
					tempMat->SetProperty("_BRDFLut", GetSceneView.GetRenderer().GetBrdfTexture());
					tempMat->SetProperty("u_Albedo", Maths::FVector4{ 1.0, 1.0, 1.0, 1.0 });

					tempMat->SetProperty("u_AlphaClippingThreshold", 1.0f);
					tempMat->SetProperty("u_Roughness", 0.1f);
					tempMat->SetProperty("u_Metallic", 0.1f);
					// Emission
					tempMat->SetProperty("u_EmissiveIntensity", 1.0f);
					tempMat->SetProperty("u_EmissiveColor", Maths::FVector3{ 0.0f,0.0f,0.0f });

				}
			
		
				auto& actor = scene->CreateActor(sceneName);
				GetViewerWidget.addActorToTreeView(&actor);
				actor.AddComponent<Core::ECS::Components::CModelRenderer>().SetModel(model);
				//actor.GetComponent<Core::ECS::Components::CTransform>()->SetMatrix(xform.data);
				auto& materilaRener = actor.AddComponent<Core::ECS::Components::CMaterialRenderer>();
				materilaRener.SetMaterialAtIndex(0, *tempMat);
				materilaRener.UpdateMaterialList();
			}

		}
		//scene->BuildSceneBvh();
	}

	void ParseScene::updateTreeViewSceneRoot()
	{

	}

	void ParseScene::updateTreeViewPathRoot()
	{
	}

	ParseScene::ParseScene(QObject* parent) :QObject(parent)
	{
	}

	//bool LoadSceneFromFile(const std::string& filename, Core::SceneSystem::Scene* scene)
	//{
	//	FILE* file;
	//	file = fopen(filename.c_str(), "r");

	//	if (!file)
	//	{
	//		CORE_ERROR("Couldn't open {0} for reading\n", filename.c_str());
	//		return false;
	//	}
	//	CORE_INFO("Loading Scene..\n");
	//	struct MaterialData
	//	{
	//		Material mat;
	//		int id;
	//	};

	//	std::string path = filename.substr(0, filename.find_last_of("/\\")) + "/";
	//	int materialCount = 0;
	//	char line[kMaxLineLength];
	//	while (fgets(line, kMaxLineLength, file))
	//	{
	//		// skip comments
	//		if (line[0] == '#')
	//			continue;
	//		// name used for materials and meshes
	//		char name[kMaxLineLength] = { 0 };
	//		if (sscanf(line, " material %s", name) == 1)
	//		{
	//			struct aMaterial
	//			{
	//				aMaterial()
	//				{
	//					baseColorR = baseColorG = baseColorB = 1.0f;
	//					//各向异性
	//					anisotropic = 0.0f;

	//					emissionR = emissionG = emissionB = 0.0f;
	//					// padding1

	//					metallic = 0.0f;
	//					roughness = 0.5f;
	//					subsurface = 0.0f;
	//					specularTint = 0.0f;

	//					sheen = 0.0f;
	//					sheenTint = 0.0f;
	//					clearcoat = 0.0f;
	//					clearcoatGloss = 0.0f;

	//					specTrans = 0.0f;
	//					ior = 1.5f;
	//					//介质类型
	//					mediumType = 0.0f;
	//					mediumDensity = 0.0f;

	//					mediumColor = Vec3(1.0f, 1.0f, 1.0f);
	//					//各向同性
	//					mediumAnisotropy = 0.0f;

	//					baseColorTexId = -1.0f;
	//					metallicRoughnessTexID = -1.0f;
	//					normalmapTexID = -1.0f;
	//					emissionmapTexID = -1.0f;

	//					opacity = 1.0f;
	//					alphaMode = 0.0f;
	//					alphaCutoff = 0.0f;
	//					// padding2
	//				};
	//				float  baseColorR;
	//				float  baseColorG;
	//				float  baseColorB;
	//				float anisotropic;

	//				float emissionR;
	//				float emissionG;
	//				float emissionB;
	//				float ax;

	//				float metallic;
	//				float roughness;
	//				float subsurface;
	//				float specularTint;

	//				float sheen;
	//				float sheenTint;
	//				float clearcoat;
	//				float clearcoatGloss;

	//				float specTrans;
	//				float ior;
	//				float mediumType;
	//				float mediumDensity;

	//				Vec3 mediumColor;
	//				float mediumAnisotropy;

	//				float baseColorTexId;
	//				float metallicRoughnessTexID;
	//				float normalmapTexID;
	//				float emissionmapTexID;

	//				float opacity;
	//				float alphaMode;
	//				float alphaCutoff;
	//				float ay;
	//			};
	//			aMaterial material;
	//			char albedoTexName[100] = "none";
	//			char metallicRoughnessTexName[100] = "none";
	//			char normalTexName[100] = "none";
	//			char emissionTexName[100] = "none";
	//			char alphaMode[20] = "none";
	//			char mediumType[20] = "none";

	//			while (fgets(line, kMaxLineLength, file))
	//			{
	//				// end group
	//				if (strchr(line, '}'))
	//					break;

	//				sscanf(line, " color %f %f %f", &material.baseColorR, &material.baseColorG, &material.baseColorB);
	//				sscanf(line, " opacity %f", &material.opacity);
	//				sscanf(line, " alphamode %s", alphaMode);
	//				sscanf(line, " alphacutoff %f", &material.alphaCutoff);
	//				sscanf(line, " emission %f %f %f", &material.emissionR, &material.emissionG, &material.emissionB);
	//				sscanf(line, " metallic %f", &material.metallic);
	//				sscanf(line, " roughness %f", &material.roughness);
	//				sscanf(line, " subsurface %f", &material.subsurface);
	//				sscanf(line, " speculartint %f", &material.specularTint);
	//				sscanf(line, " anisotropic %f", &material.anisotropic);
	//				sscanf(line, " sheen %f", &material.sheen);
	//				sscanf(line, " sheentint %f", &material.sheenTint);
	//				sscanf(line, " clearcoat %f", &material.clearcoat);
	//				sscanf(line, " clearcoatgloss %f", &material.clearcoatGloss);
	//				sscanf(line, " spectrans %f", &material.specTrans);
	//				sscanf(line, " ior %f", &material.ior);
	//				sscanf(line, " albedotexture %s", albedoTexName);
	//				sscanf(line, " metallicroughnesstexture %s", metallicRoughnessTexName);
	//				sscanf(line, " normaltexture %s", normalTexName);
	//				sscanf(line, " emissiontexture %s", emissionTexName);
	//				sscanf(line, " mediumtype %s", mediumType);
	//				sscanf(line, " mediumdensity %f", &material.mediumDensity);
	//				sscanf(line, " mediumcolor %f %f %f", &material.mediumColor.x, &material.mediumColor.y, &material.mediumColor.z);
	//				sscanf(line, " mediumanisotropy %f", &material.mediumAnisotropy);
	//			}
	//			Core::Resources::Material* tempMat = new Core::Resources::Material();
	//			Core::Global::ServiceLocator::Get<Core::ResourceManagement::MaterialManager>().RegisterResource(name, tempMat);
	//			tempMat->SetShader(Core::Global::ServiceLocator::Get<Editor::Core::Context>().shaderManager[":Shaders\\Standard.ovfx"]);
	//			tempMat->SetProperty("u_Albedo", Maths::FVector4{ material.baseColorR, material.baseColorG, material.baseColorB, material.opacity });
	//			tempMat->SetProperty("u_EmissiveColor", Maths::FVector3{ material.emissionR, material.emissionG, material.emissionB });
	//			tempMat->SetProperty("u_Metallic", material.metallic);
	//			tempMat->SetProperty("u_Roughness", material.roughness);
	//			tempMat->SetProperty("u_RefractionIndex", material.ior);
	//			tempMat->SetProperty("u_EmissiveIntensity", 1.0f);
	//			//tempMat->SetProperty("u_Diffuse", Maths::FVector4{ 1.0,  1.0, 1.0, 1.0f });
	//			tempMat->SetBackfaceCulling(false);;
	//			tempMat->SetCastShadows(false);
	//			tempMat->SetReceiveShadows(false);
	//			// Albedo Texture
	//			auto albedo = Core::Global::ServiceLocator::Get<Core::ResourceManagement::TextureManager>().GetResource(path + albedoTexName, true);
	//			tempMat->SetProperty("u_AlbedoMap", albedo);


	//			// MetallicRoughness Texture
	//			auto roughness = Core::Global::ServiceLocator::Get<Core::ResourceManagement::TextureManager>().GetResource(path + metallicRoughnessTexName, true);
	//			tempMat->SetProperty("u_RoughnessMap", roughness);
	//			auto emission = Core::Global::ServiceLocator::Get<Core::ResourceManagement::TextureManager>().GetResource(path + emissionTexName, true);
	//			tempMat->SetProperty("u_EmissiveMap", emission);
	//			auto metallicMap = Core::Global::ServiceLocator::Get<Core::ResourceManagement::TextureManager>().GetResource(path + metallicRoughnessTexName, true);
	//			tempMat->SetProperty("u_MetallicMap", metallicMap);
	//			// Normal Map Texture
	//			auto normalTex = Core::Global::ServiceLocator::Get<Core::ResourceManagement::TextureManager>().GetResource(path + normalTexName, true);
	//			tempMat->SetProperty("u_NormalMap", normalTex);
	//			if (normalTex)
	//				tempMat->AddFeature("NORMAL_MAPPING");


	//			// AlphaMode
	//			if (strcmp(alphaMode, "opaque") == 0)
	//				material.alphaMode = AlphaMode::Opaque;
	//			else if (strcmp(alphaMode, "blend") == 0)
	//				material.alphaMode = AlphaMode::Blend;
	//			else if (strcmp(alphaMode, "mask") == 0)
	//				material.alphaMode = AlphaMode::Mask;

	//			// MediumType
	//			if (strcmp(mediumType, "absorb") == 0)
	//				material.mediumType = MediumType::Absorb;
	//			else if (strcmp(mediumType, "scatter") == 0)
	//			{
	//				material.mediumType = MediumType::Scatter;
	//				tempMat->AddFeature("SPECULAR_WORKFLOW");
	//			}

	//			else if (strcmp(mediumType, "emissive") == 0)
	//				material.mediumType = MediumType::Emissive;

	//		}
	//		// Mesh
	//		if (strstr(line, "mesh"))
	//		{
	//			std::string filename;
	//			Vec4 rotQuat;
	//			Mat4 xform, translate, rot, scale;

	//			int material_id = 0; // Default Material ID
	//			int parentid = -1;
	//			char meshName[200] = "none";
	//			char parentName[200] = "none";
	//			bool matrixPrided = false;
	//			char matName[100];
	//			while (fgets(line, kMaxLineLength, file))
	//			{
	//				// end group
	//				if (strchr(line, '}'))
	//					break;

	//				char file[2048];


	//				sscanf(line, " name %[^\t\n]s", meshName);
	//				if (sscanf(line, " parent %s", parentName) == 1)
	//				{
	//					//in this case parent must prior to this meshinstance
	//					//parentid = InstanceMap[parentName];

	//				}
	//				if (sscanf(line, " file %s", file) == 1)
	//					filename = path + file;

	//				if (sscanf(line, " material %s", matName) == 1)
	//				{

	//				}

	//				if (sscanf(line, " matrix %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f",
	//					&xform[0][0], &xform[1][0], &xform[2][0], &xform[3][0],
	//					&xform[0][1], &xform[1][1], &xform[2][1], &xform[3][1],
	//					&xform[0][2], &xform[1][2], &xform[2][2], &xform[3][2],
	//					&xform[0][3], &xform[1][3], &xform[2][3], &xform[3][3]
	//				) != 0)
	//					matrixPrided = true;

	//				sscanf(line, " position %f %f %f", &translate.data[3][0], &translate.data[3][1], &translate.data[3][2]);
	//				sscanf(line, " scale %f %f %f", &scale.data[0][0], &scale.data[1][1], &scale.data[2][2]);
	//				if (sscanf(line, " rotation %f %f %f %f", &rotQuat.x, &rotQuat.y, &rotQuat.z, &rotQuat.w) != 0)
	//					rot = Mat4::QuatToMatrix(rotQuat.x, rotQuat.y, rotQuat.z, rotQuat.w);
	//			}

	//			if (!filename.empty())
	//			{
	//				auto& actor = scene->CreateActor();
	//				if (strcmp(meshName, "none") != 0)
	//				{
	//					actor.SetName(meshName);
	//				}
	//				auto mesh = Core::Global::ServiceLocator::Get<Core::ResourceManagement::ModelManager>().GetResource(filename);
	//				actor.AddComponent<Core::ECS::Components::CModelRenderer>().SetModel(mesh);
	//				Mat4 transformMat;

	//				if (matrixPrided)
	//					transformMat = xform;
	//				else
	//					transformMat = scale * rot * translate;
	//				actor.GetComponent<Core::ECS::Components::CTransform>()->SetMatrix(transformMat.data);
	//				auto& materilaRener = actor.AddComponent<Core::ECS::Components::CMaterialRenderer>();
	//				auto mat = Core::Global::ServiceLocator::Get<Core::ResourceManagement::MaterialManager>().GetResource(matName);
	//				materilaRener.SetMaterialAtIndex(0, *mat);
	//				materilaRener.UpdateMaterialList();

	//			}
	//		}

	//		if (strstr(line, "gltf"))
	//		{
	//			std::string filename;
	//			Vec4 rotQuat;
	//			Mat4 xform, translate, rot, scale;
	//			bool matrixPrided = false;

	//			while (fgets(line, kMaxLineLength, file))
	//			{
	//				// end group
	//				if (strchr(line, '}'))
	//					break;

	//				char file[2048];

	//				if (sscanf(line, " file %s", file) == 1)
	//					filename = path + file;

	//				if (sscanf(line, " matrix %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f",
	//					&xform[0][0], &xform[1][0], &xform[2][0], &xform[3][0],
	//					&xform[0][1], &xform[1][1], &xform[2][1], &xform[3][1],
	//					&xform[0][2], &xform[1][2], &xform[2][2], &xform[3][2],
	//					&xform[0][3], &xform[1][3], &xform[2][3], &xform[3][3]
	//				) != 0)
	//					matrixPrided = true;

	//				sscanf(line, " position %f %f %f", &translate.data[3][0], &translate.data[3][1], &translate.data[3][2]);
	//				sscanf(line, " scale %f %f %f", &scale.data[0][0], &scale.data[1][1], &scale.data[2][2]);
	//				if (sscanf(line, " rotation %f %f %f %f", &rotQuat.x, &rotQuat.y, &rotQuat.z, &rotQuat.w) != 0)
	//					rot = Mat4::QuatToMatrix(rotQuat.x, rotQuat.y, rotQuat.z, rotQuat.w);
	//			}

	//			if (!filename.empty())
	//			{
	//				std::string ext = filename.substr(filename.find_last_of(".") + 1);

	//				bool success = false;
	//				Mat4 transformMat;

	//				if (matrixPrided)
	//					transformMat = xform;
	//				else
	//					transformMat = scale * rot * translate;

	//				// TODO: Add support for instancing.
	//				// If the same gltf is loaded multiple times then mesh data gets duplicated
	//				if (ext == "gltf")
	//					success = LoadGLTF(filename, scene, transformMat, false);
	//				else if (ext == "glb")
	//					success = LoadGLTF(filename, scene, transformMat, true);

	//				if (!success)
	//				{
	//					printf("Unable to load gltf %s\n", filename.c_str());
	//					exit(0);
	//				}
	//			}
	//		}
	//	}
	//	fclose(file);
	//	return true;
	//}


	bool LoadGLTF(const std::string& filename, Core::SceneSystem::Scene* scene, bool binary)
	{
		tinygltf::Model gltfModel;
		tinygltf::TinyGLTF loader;
		std::string err;
		std::string warn;

		CORE_INFO("Loading GLTF{0}", filename.c_str());

		bool ret;

		if (binary)
			ret = loader.LoadBinaryFromFile(&gltfModel, &err, &warn, filename);
		else
			ret = loader.LoadASCIIFromFile(&gltfModel, &err, &warn, filename);

		if (!ret)
		{
			CORE_ERROR("Unable to load file {0}. Error: {1}\n", filename.c_str(), err.c_str());
			return false;
		}
		struct primIndex
		{
			std::string path;
			int materialIndex;
		};
		std::map<int, std::vector<primIndex>> meshPrimMap;
		std::unordered_map<int, std::string>textureMap;
		std::unordered_map<int, std::string>materilaMap;
		//Load Mesh
		{
			for (int gltfMeshIdx = 0; gltfMeshIdx < gltfModel.meshes.size(); gltfMeshIdx++)
			{
				tinygltf::Mesh gltfMesh = gltfModel.meshes[gltfMeshIdx];

				for (int gltfPrimIdx = 0; gltfPrimIdx < gltfMesh.primitives.size(); gltfPrimIdx++)
				{
					tinygltf::Primitive prim = gltfMesh.primitives[gltfPrimIdx];

					// Skip points and lines
					if (prim.mode != TINYGLTF_MODE_TRIANGLES)
						continue;

					int indicesIndex = prim.indices;
					int positionIndex = -1;
					int normalIndex = -1;
					int uv0Index = -1;

					if (prim.attributes.count("POSITION") > 0)
					{
						positionIndex = prim.attributes["POSITION"];
					}

					if (prim.attributes.count("NORMAL") > 0)
					{
						normalIndex = prim.attributes["NORMAL"];
					}

					if (prim.attributes.count("TEXCOORD_0") > 0)
					{
						uv0Index = prim.attributes["TEXCOORD_0"];
					}


					// Vertex positions
					tinygltf::Accessor positionAccessor = gltfModel.accessors[positionIndex];
					tinygltf::BufferView positionBufferView = gltfModel.bufferViews[positionAccessor.bufferView];
					const tinygltf::Buffer& positionBuffer = gltfModel.buffers[positionBufferView.buffer];
					const uint8_t* positionBufferAddress = positionBuffer.data.data();
					int positionStride = tinygltf::GetComponentSizeInBytes(positionAccessor.componentType) * tinygltf::GetNumComponentsInType(positionAccessor.type);
					// TODO: Recheck
					if (positionBufferView.byteStride > 0)
						positionStride = positionBufferView.byteStride;

					// FIXME: Some GLTF files like TriangleWithoutIndices.gltf have no indices
					// Vertex indices
					tinygltf::Accessor indexAccessor = gltfModel.accessors[indicesIndex];
					tinygltf::BufferView indexBufferView = gltfModel.bufferViews[indexAccessor.bufferView];
					const tinygltf::Buffer& indexBuffer = gltfModel.buffers[indexBufferView.buffer];
					const uint8_t* indexBufferAddress = indexBuffer.data.data();
					int indexStride = tinygltf::GetComponentSizeInBytes(indexAccessor.componentType) * tinygltf::GetNumComponentsInType(indexAccessor.type);

					// Normals
					tinygltf::Accessor normalAccessor;
					tinygltf::BufferView normalBufferView;
					const uint8_t* normalBufferAddress = nullptr;
					int normalStride = -1;
					if (normalIndex > -1)
					{
						normalAccessor = gltfModel.accessors[normalIndex];
						normalBufferView = gltfModel.bufferViews[normalAccessor.bufferView];
						const tinygltf::Buffer& normalBuffer = gltfModel.buffers[normalBufferView.buffer];
						normalBufferAddress = normalBuffer.data.data();
						normalStride = tinygltf::GetComponentSizeInBytes(normalAccessor.componentType) * tinygltf::GetNumComponentsInType(normalAccessor.type);
						if (normalBufferView.byteStride > 0)
							normalStride = normalBufferView.byteStride;
					}

					// Texture coordinates
					tinygltf::Accessor uv0Accessor;
					tinygltf::BufferView uv0BufferView;
					const uint8_t* uv0BufferAddress = nullptr;
					int uv0Stride = -1;
					if (uv0Index > -1)
					{
						uv0Accessor = gltfModel.accessors[uv0Index];
						uv0BufferView = gltfModel.bufferViews[uv0Accessor.bufferView];
						const tinygltf::Buffer& uv0Buffer = gltfModel.buffers[uv0BufferView.buffer];
						uv0BufferAddress = uv0Buffer.data.data();
						uv0Stride = tinygltf::GetComponentSizeInBytes(uv0Accessor.componentType) * tinygltf::GetNumComponentsInType(uv0Accessor.type);
						if (uv0BufferView.byteStride > 0)
							uv0Stride = uv0BufferView.byteStride;
					}

					std::vector<Maths::FVector3> vertices;
					std::vector<Maths::FVector3> normals;
					std::vector<Maths::FVector2> uvs;

					// Get vertex data
					for (size_t vertexIndex = 0; vertexIndex < positionAccessor.count; vertexIndex++)
					{
						Maths::FVector3 vertex, normal;
						Maths::FVector2 uv;

						{
							const uint8_t* address = positionBufferAddress + positionBufferView.byteOffset + positionAccessor.byteOffset + (vertexIndex * positionStride);
							memcpy(&vertex, address, 12);
						}

						if (normalIndex > -1)
						{
							const uint8_t* address = normalBufferAddress + normalBufferView.byteOffset + normalAccessor.byteOffset + (vertexIndex * normalStride);
							memcpy(&normal, address, 12);
						}

						if (uv0Index > -1)
						{
							const uint8_t* address = uv0BufferAddress + uv0BufferView.byteOffset + uv0Accessor.byteOffset + (vertexIndex * uv0Stride);
							memcpy(&uv, address, 8);
						}

						vertices.push_back(vertex);
						normals.push_back(normal);
						uvs.push_back({ uv.x,1 - uv.y });
					}

					// Get index data
					std::vector<unsigned int> indices(indexAccessor.count);
					const uint8_t* baseAddress = indexBufferAddress + indexBufferView.byteOffset + indexAccessor.byteOffset;
					if (indexStride == 1)
					{
						std::vector<uint8_t> quarter;
						quarter.resize(indexAccessor.count);

						memcpy(quarter.data(), baseAddress, (indexAccessor.count * indexStride));

						// Convert quarter precision indices to full precision
						for (size_t i = 0; i < indexAccessor.count; i++)
						{
							indices[i] = quarter[i];
						}
					}
					else if (indexStride == 2)
					{
						std::vector<uint16_t> half;
						half.resize(indexAccessor.count);

						memcpy(half.data(), baseAddress, (indexAccessor.count * indexStride));

						// Convert half precision indices to full precision
						for (size_t i = 0; i < indexAccessor.count; i++)
						{
							indices[i] = half[i];
						}
					}
					else
					{
						memcpy(indices.data(), baseAddress, (indexAccessor.count * indexStride));
					}
					std::string modelname = filename + std::to_string(gltfMeshIdx) + "#" + std::to_string(gltfPrimIdx);
					Core::Global::ServiceLocator::Get<Core::ResourceManagement::ModelManager>().LoadFromMemory(modelname, vertices, normals, uvs, indices);

					meshPrimMap[gltfMeshIdx].push_back(primIndex{ modelname, prim.material });
				}
			}
		}
		//Load Texture
		{
			for (size_t i = 0; i < gltfModel.textures.size(); ++i)
			{
				tinygltf::Texture& gltfTex = gltfModel.textures[i];
				tinygltf::Image& image = gltfModel.images[gltfTex.source];
				std::string texName = gltfTex.name;
				if (strcmp(gltfTex.name.c_str(), "") == 0)
					texName = image.uri;
				Core::Global::ServiceLocator::Get<Core::ResourceManagement::TextureManager>().CreateFromMemory(texName, image.image.data(), image.width, image.height);
				textureMap[i] = texName;
			}
		}
		//Load Material
		{

			// TODO: Support for KHR extensions
			for (size_t i = 0; i < gltfModel.materials.size(); i++)
			{
				const tinygltf::Material gltfMaterial = gltfModel.materials[i];
				const tinygltf::PbrMetallicRoughness pbr = gltfMaterial.pbrMetallicRoughness;

				// Convert glTF material

				materilaMap[i] = gltfMaterial.name;
				if (materilaMap[i] == "") {
					materilaMap[i] = filename + "::material " + std::to_string(i);
				}

				Core::Resources::Material* tempMat = new Core::Resources::Material();
				Core::Global::ServiceLocator::Get<Core::ResourceManagement::MaterialManager>().RegisterResource(materilaMap[i], tempMat);
				tempMat->AddFeature("NORMAL_MAPPING");
				tempMat->AddFeature("CLIP_PLANE");
				tempMat->SetShader(Core::Global::ServiceLocator::Get<Editor::Core::Context>().shaderManager[":Shaders\\Standard.ovfx"]);
				// Overrides after the shader: SetShader now takes the state from the material block.
				tempMat->SetBackfaceCulling(false);
				tempMat->SetCastShadows(false);
				tempMat->SetReceiveShadows(false);
				tempMat->SetProperty("u_Albedo", Maths::FVector4{ (float)pbr.baseColorFactor[0], (float)pbr.baseColorFactor[1], (float)pbr.baseColorFactor[2], (float)pbr.baseColorFactor[3] });
				// Albedo Texture
				if (pbr.baseColorTexture.index > -1) {
					auto albedo = Core::Global::ServiceLocator::Get<Core::ResourceManagement::TextureManager>().GetResource(textureMap[pbr.baseColorTexture.index], true);
					tempMat->SetProperty("u_AlbedoMap", albedo);
				}
				tempMat->SetProperty("u_AlphaClippingThreshold", static_cast<float>(gltfMaterial.alphaCutoff));
				tempMat->SetProperty("u_Roughness", (float)pbr.roughnessFactor);
				tempMat->SetProperty("u_Metallic", (float)pbr.metallicFactor);
				//if (strcmp(gltfMaterial.alphaMode.c_str(), "OPAQUE") == 0) material.alphaMode = AlphaMode::Opaque;
				//else if (strcmp(gltfMaterial.alphaMode.c_str(), "BLEND") == 0) material.alphaMode = AlphaMode::Blend;
				//else if (strcmp(gltfMaterial.alphaMode.c_str(), "MASK") == 0) material.alphaMode = AlphaMode::Mask;

				// MetallicRoughness Texture
				if (pbr.metallicRoughnessTexture.index > -1) {
					auto roughness = Core::Global::ServiceLocator::Get<Core::ResourceManagement::TextureManager>().GetResource(textureMap[pbr.metallicRoughnessTexture.index], true);
					tempMat->SetProperty("u_RoughnessMap", roughness);

					auto metallicMap = Core::Global::ServiceLocator::Get<Core::ResourceManagement::TextureManager>().GetResource(textureMap[pbr.metallicRoughnessTexture.index], true);
					tempMat->SetProperty("u_MetallicMap", metallicMap);
				}
				if (gltfMaterial.emissiveTexture.index > -1) {
					auto emission = Core::Global::ServiceLocator::Get<Core::ResourceManagement::TextureManager>().GetResource(textureMap[gltfMaterial.emissiveTexture.index], true);
					tempMat->SetProperty("u_EmissiveMap", emission);
				}
				// Normal Map Texture
				auto normalTex = Core::Global::ServiceLocator::Get<Core::ResourceManagement::TextureManager>().GetResource(textureMap[gltfMaterial.normalTexture.index], true);
				tempMat->SetProperty("u_NormalMap", normalTex);
				// Emission
				tempMat->SetProperty("u_EmissiveIntensity", 1.0f);
				tempMat->SetProperty("u_EmissiveColor", Maths::FVector3{ (float)gltfMaterial.emissiveFactor[0], (float)gltfMaterial.emissiveFactor[1], (float)gltfMaterial.emissiveFactor[2] });
				// KHR_materials_transmission
				if (gltfMaterial.extensions.find("KHR_materials_transmission") != gltfMaterial.extensions.end())
				{
					const auto& ext = gltfMaterial.extensions.find("KHR_materials_transmission")->second;
					if (ext.Has("transmissionFactor"))
					{
						tempMat->SetProperty("u_Transmission", (float)(ext.Get("transmissionFactor").Get<double>()));
					}
				}
			}
		}
		//Load Actor
		{
			const tinygltf::Scene gltfScene = gltfModel.scenes[gltfModel.defaultScene];

			for (int rootIdx = 0; rootIdx < gltfScene.nodes.size(); rootIdx++)
			{
				auto& rootActor = scene->CreateActor();
				GetViewerWidget.addActorToTreeView(&rootActor);
				rootActor.SetName("Root " + std::to_string(rootIdx));
				std::vector<int>indexStack;
				//std::vector<Mat4>transformStack;
				std::vector<int64_t>parentStack;
				indexStack.push_back(gltfScene.nodes[rootIdx]);
				//transformStack.push_back(formaa);
				parentStack.push_back(rootActor.GetID());
				while (!indexStack.empty())
				{
					int nodeIdx = indexStack.back(); indexStack.pop_back();
					//Mat4 parentMat = transformStack.back(); transformStack.pop_back();
					int parentActorIdx = parentStack.back(); parentStack.pop_back();


					tinygltf::Node gltfNode = gltfModel.nodes[nodeIdx];
					Maths::FMatrix4 localMat;
					if (gltfNode.matrix.size() > 0)
					{
						localMat(0,0) = gltfNode.matrix[0];
						localMat(0,1) = gltfNode.matrix[1];
						localMat(0,2) = gltfNode.matrix[2];
						localMat(0,3) = gltfNode.matrix[3];

						localMat(1,0) = gltfNode.matrix[4];
						localMat(1,1) = gltfNode.matrix[5];
						localMat(1,2) = gltfNode.matrix[6];
						localMat(1,3) = gltfNode.matrix[7];

						localMat(2,0) = gltfNode.matrix[8];
						localMat(2,1) = gltfNode.matrix[9];
						localMat(2,2) = gltfNode.matrix[10];
						localMat(2,3) = gltfNode.matrix[11];

						localMat(3,0) = gltfNode.matrix[12];
						localMat(3,1) = gltfNode.matrix[13];
						localMat(3,2) = gltfNode.matrix[14];
						localMat(3,3) = gltfNode.matrix[15];
					}
					else
					{
						Maths::FMatrix4 translate, rot, scale;
						
						if (gltfNode.translation.size() > 0)
						{
							translate(3,0) = gltfNode.translation[0];
							translate(3,1) = gltfNode.translation[1];
							translate(3,2) = gltfNode.translation[2];
						}

						if (gltfNode.rotation.size() > 0)
						{
							rot =Maths::FQuaternion::ToMatrix4(Maths::FQuaternion(static_cast<float>(gltfNode.rotation[0]), static_cast<float>(gltfNode.rotation[1]), static_cast<float>(gltfNode.rotation[2]), static_cast<float>(gltfNode.rotation[3])));
							
						}

						if (gltfNode.scale.size() > 0)
						{
							scale(0,0) = gltfNode.scale[0];
							scale(1,1) = gltfNode.scale[1];
							scale(2,2) = gltfNode.scale[2];
						}

						localMat = scale * rot * translate;
						//localMat = translate * rot * scale;
					}

					//Mat4 xform = localMat * parentMat;
					//localMat = localMat.Transpose();
					// When at a leaf node, add an instance to the scene (if a mesh exists for it)
					if (gltfNode.children.size() == 0 && gltfNode.mesh != -1)
					{
						std::vector<primIndex> prims = meshPrimMap[gltfNode.mesh];

						// Write the instance data
						for (int i = 0; i < prims.size(); i++)
						{
							std::string name = gltfNode.name;
							// TODO: Better naming
							if (strcmp(name.c_str(), "") == 0)
								name = "Mesh " + std::to_string(gltfNode.mesh) + " Prim" + prims[i].path;
							auto& actor = scene->CreateActor();
							GetViewerWidget.addActorToTreeView(&actor);
							actor.SetParent(*scene->FindActorByID(parentActorIdx));
							actor.SetName(name);

							auto mesh = Core::Global::ServiceLocator::Get<Core::ResourceManagement::ModelManager>().GetResource(prims[i].path);
							actor.AddComponent<Core::ECS::Components::CModelRenderer>().SetModel(mesh);

							actor.GetComponent<Core::ECS::Components::CTransform>()->SetMatrix(localMat.data);
							auto& materilaRener = actor.AddComponent<Core::ECS::Components::CMaterialRenderer>();
							if (prims[i].materialIndex < 0) {
								assert(false);
							}

							auto mat = Core::Global::ServiceLocator::Get<Core::ResourceManagement::MaterialManager>().GetResource(materilaMap[prims[i].materialIndex < 0 ? 0 : prims[i].materialIndex]);
							materilaRener.SetMaterialAtIndex(0, *mat);
							materilaRener.UpdateMaterialList();

						}
					}
					else {
						std::string name = gltfNode.name;
						// TODO: Better naming
						if (strcmp(name.c_str(), "") == 0)
							name = "Mesh #";
						auto& actor = scene->CreateActor();
						GetViewerWidget.addActorToTreeView(&actor);
						actor.SetParent(*scene->FindActorByID(parentActorIdx));
						actor.SetName(name);
						actor.GetComponent<Core::ECS::Components::CTransform>()->SetMatrix(localMat.data);
						for (size_t i = 0; i < gltfNode.children.size(); i++)
						{
							indexStack.push_back(gltfNode.children[i]);
							//transformStack.push_back(xform);
							parentStack.push_back(actor.GetID());
						}
					}
				}
			}
		}
		return true;
	}

}
