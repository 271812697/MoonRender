#include <algorithm>
#include <string>
#include <tinyxml2.h>
#include <tracy/Tracy.hpp>
#include <Core/ECS/Components/CAmbientSphereLight.h>
#include <Core/ECS/Components/CDirectionalLight.h>
#include <Core/ECS/Components/CMaterialRenderer.h>
#include <Core/Global/ServiceLocator.h>
#include <Core/ResourceManagement/MaterialManager.h>
#include <Core/ResourceManagement/ModelManager.h>
#include <Core/SceneSystem/Scene.h>
#include <Core/SceneSystem/BvhService.h>


Core::SceneSystem::Scene::Scene()
{
	bvhService = new BvhService(this);
}

Core::SceneSystem::Scene::~Scene()
{
	std::for_each(m_actors.begin(), m_actors.end(), [](Core::ECS::Actor* element)
	{ 
		delete element;
	});
	m_actors.clear();
	delete bvhService;
}

void Core::SceneSystem::Scene::AddDefaultCamera()
{
	auto& camera = CreateActor("Main Camera");
	camera.AddComponent<ECS::Components::CCamera>();
	camera.transform.SetLocalPosition({ 0.0f, 3.0f, 8.0f });
	camera.transform.SetLocalRotation(Maths::FQuaternion({ 20.0f, 180.0f, 0.0f }));
}

void Core::SceneSystem::Scene::AddDefaultLights()
{
	auto& directionalLight = CreateActor("Directional Light");
	directionalLight.SetTag("DirectionalLight");
	directionalLight.AddComponent<ECS::Components::CDirectionalLight>().SetIntensity(1.0f);
	directionalLight.transform.SetLocalPosition({ 0.0f, 10.0f, 0.0f });
	directionalLight.transform.SetLocalRotation(Maths::FQuaternion({ 120.0f, -40.0f, 0.0f }));
}

void Core::SceneSystem::Scene::AddDefaultReflections()
{
	auto& reflectionProbe = CreateActor("Reflection Probe");
	reflectionProbe.AddComponent<ECS::Components::CReflectionProbe>();
	reflectionProbe.transform.SetLocalPosition({ 0.0f, 3.0f, 0.0f });
}

void Core::SceneSystem::Scene::AddDefaultPostProcessStack()
{
	auto& postProcessStack = CreateActor("Post Process Stack");
	postProcessStack.SetTag("PostProcessStack");
	postProcessStack.AddComponent<ECS::Components::CPostProcessStack>();
}

void Core::SceneSystem::Scene::AddDefaultSkysphere()
{
	auto& skysphere = CreateActor("Skysphere");
	skysphere.SetTag("SkyBox");
	auto& materialRenderer = skysphere.AddComponent<ECS::Components::CMaterialRenderer>();
	auto& modelRenderer = skysphere.AddComponent<ECS::Components::CModelRenderer>();
	modelRenderer.SetFrustumBehaviour(ECS::Components::CModelRenderer::EFrustumBehaviour::DISABLED);

	auto skysphereMaterial = Global::ServiceLocator::Get<ResourceManagement::MaterialManager>().GetResource(":Materials\\Skysphere.ovmat");
	auto sphereModel = Global::ServiceLocator::Get<ResourceManagement::ModelManager>().GetResource(":Models\\Sphere.fbx");
	
	if (skysphereMaterial)
	{
		materialRenderer.SetMaterialAtIndex(0, *skysphereMaterial);
	}

	if (sphereModel)
	{
		modelRenderer.SetModel(sphereModel);
	}
}

void Core::SceneSystem::Scene::AddDefaultAtmosphere()
{
	auto& atmosphere = CreateActor("Atmoshpere");
	auto& materialRenderer = atmosphere.AddComponent<ECS::Components::CMaterialRenderer>();
	auto& modelRenderer = atmosphere.AddComponent<ECS::Components::CModelRenderer>();
	modelRenderer.SetFrustumBehaviour(ECS::Components::CModelRenderer::EFrustumBehaviour::DISABLED);

	auto atmosphereMaterial = Global::ServiceLocator::Get<ResourceManagement::MaterialManager>().GetResource(":Materials\\Atmosphere.ovmat");
	auto sphereModel = Global::ServiceLocator::Get<ResourceManagement::ModelManager>().GetResource(":Models\\Sphere.fbx");

	if (atmosphereMaterial)
	{
		materialRenderer.SetMaterialAtIndex(0, *atmosphereMaterial);
	}

	if (sphereModel)
	{
		modelRenderer.SetModel(sphereModel);
	}
}

void Core::SceneSystem::Scene::Play()
{
	m_isPlaying = true;

	/* Wake up actors to allow them to react to OnEnable, OnDisable and OnDestroy, */
	std::for_each(m_actors.begin(), m_actors.end(), [](ECS::Actor * p_element) { p_element->SetSleeping(false); });

	std::for_each(m_actors.begin(), m_actors.end(), [](ECS::Actor * p_element) { if (p_element->IsActive()) p_element->OnAwake(); });
	std::for_each(m_actors.begin(), m_actors.end(), [](ECS::Actor * p_element) { if (p_element->IsActive()) p_element->OnEnable(); });
	std::for_each(m_actors.begin(), m_actors.end(), [](ECS::Actor * p_element) { if (p_element->IsActive()) p_element->OnStart(); });
}

bool Core::SceneSystem::Scene::IsPlaying() const
{
	return m_isPlaying;
}

void Core::SceneSystem::Scene::Update(float p_deltaTime)
{
	ZoneScoped;
	auto actors = m_actors;
	{
		ZoneScopedN("Actor::OnUpdate");
		std::for_each(actors.begin(), actors.end(), std::bind(std::mem_fn(&ECS::Actor::OnUpdate), std::placeholders::_1, p_deltaTime));
	}
	{
		ZoneScopedN("DestoryActors");
		for (int i = 0; i < m_delayActors.size(); i++) {
			DestroyActor(*m_delayActors[i]);
		}
		m_delayActors.clear();
	}

}

void Core::SceneSystem::Scene::FixedUpdate(float p_deltaTime)
{
	ZoneScoped;
	auto actors = m_actors;
	std::for_each(actors.begin(), actors.end(), std::bind(std::mem_fn(&ECS::Actor::OnFixedUpdate), std::placeholders::_1, p_deltaTime));
}

void Core::SceneSystem::Scene::LateUpdate(float p_deltaTime)
{
	ZoneScoped;
	auto actors = m_actors;
	std::for_each(actors.begin(), actors.end(), std::bind(std::mem_fn(&ECS::Actor::OnLateUpdate), std::placeholders::_1, p_deltaTime));
}

Core::ECS::Actor& Core::SceneSystem::Scene::CreateActor()
{
	return CreateActor("New Actor");
}

Core::ECS::Actor& Core::SceneSystem::Scene::CreateActor(const std::string& p_name, const std::string& p_tag)
{
	Core::ECS::Actor* actor = new Core::ECS::Actor(m_availableID++, p_name != "" ? p_name : std::to_string(m_availableID), p_tag, m_isPlaying);
	m_actors.push_back(actor);
	m_actorIdMap[actor->GetID()] = actor;
	ECS::Actor& instance = *m_actors.back();
	instance.ComponentAddedEvent	+= std::bind(&Scene::OnComponentAdded, this, std::placeholders::_1);
	instance.ComponentRemovedEvent	+= std::bind(&Scene::OnComponentRemoved, this, std::placeholders::_1);
	if (m_isPlaying)
	{
		instance.SetSleeping(false);
		if (instance.IsActive())
		{
			instance.OnAwake();
			instance.OnEnable();
			instance.OnStart();
		}
	}
	return instance;
}
void Core::SceneSystem::Scene::RemoveActor(ECS::Actor* p_target)
{
	if (p_target) {
		m_actorIdMap.erase(p_target->GetID());
		auto found = std::find_if(m_actors.begin(), m_actors.end(), [&p_target](Core::ECS::Actor* element)
			{
				return element == p_target;
			});
		if (found != m_actors.end())
		{
			m_actors.erase(found);	
			auto childs = p_target->GetChildren();
			for (int i = 0;i < childs.size();i++) {
				RemoveActor(childs[i]);
			}
			for (int i = 0;i < childs.size();i++) {
				delete childs[i];
			}
			childs.clear();
		}
	}
}
void Core::SceneSystem::Scene::AddActor(ECS::Actor* p_target)
{
	if (p_target != nullptr) {
		m_actors.push_back(p_target);
		m_actorIdMap[p_target->GetID()] = p_target;
		ECS::Actor& instance = *m_actors.back();
		instance.ComponentAddedEvent += std::bind(&Scene::OnComponentAdded, this, std::placeholders::_1);
		instance.ComponentRemovedEvent += std::bind(&Scene::OnComponentRemoved, this, std::placeholders::_1);
		if (m_isPlaying)
		{
			instance.SetSleeping(false);
			if (instance.IsActive())
			{
				instance.OnAwake();
				instance.OnEnable();
				instance.OnStart();
			}
		}
	}
}

bool Core::SceneSystem::Scene::DestroyActor(ECS::Actor& p_target)
{
	auto found = std::find_if(m_actors.begin(), m_actors.end(), [&p_target](Core::ECS::Actor* element)
	{
		return element == &p_target;
	});

	if (found != m_actors.end())
	{		
		m_actorIdMap.erase(p_target.GetID());
		delete *found;
		m_actors.erase(found);
		return true;
	}
	else
	{
		return false;
	}
}

bool Core::SceneSystem::Scene::DelayDestroyActor(ECS::Actor& p_target)
{
	m_delayActors.push_back(&p_target);
	return true;
}

bool Core::SceneSystem::Scene::DelayDestroyActor(const std::vector<ECS::Actor*>& p_target)
{
	m_delayActors.insert(m_delayActors.end(),p_target.begin(),p_target.end());
	return true;
}

void Core::SceneSystem::Scene::CollectGarbages()
{
	m_actors.erase(std::remove_if(m_actors.begin(), m_actors.end(), [this](ECS::Actor* element)
	{ 
		bool isGarbage = !element->IsAlive();
		if (isGarbage)
		{
			delete element;
		}
		return isGarbage;
	}), m_actors.end());
}

Core::ECS::Actor* Core::SceneSystem::Scene::FindActorByName(const std::string& p_name) const
{
	auto result = std::find_if(m_actors.begin(), m_actors.end(), [p_name](Core::ECS::Actor* element)
	{ 
		return element->GetName() == p_name;
	});

	if (result != m_actors.end())
		return *result;
	else
		return nullptr;
}

Core::ECS::Actor* Core::SceneSystem::Scene::FindActorByTag(const std::string & p_tag) const
{
	auto result = std::find_if(m_actors.begin(), m_actors.end(), [p_tag](Core::ECS::Actor* element)
	{
		return element->GetTag() == p_tag;
	});

	if (result != m_actors.end())
		return *result;
	else
		return nullptr;
}

Core::ECS::Actor* Core::SceneSystem::Scene::FindActorByID(int64_t p_id) const
{
	if (m_actorIdMap.find(p_id) != m_actorIdMap.end()) {
		return m_actorIdMap.at(p_id);
	}
    return nullptr;
}

int64_t Core::SceneSystem::Scene::GetAvailableID() 
{
	return m_availableID++;
}

std::vector<std::reference_wrapper<Core::ECS::Actor>> Core::SceneSystem::Scene::FindActorsByName(const std::string & p_name) const
{
	std::vector<std::reference_wrapper<Core::ECS::Actor>> actors;

	for (auto actor : m_actors)
	{
		if (actor->GetName() == p_name)
			actors.push_back(std::ref(*actor));
	}

	return actors;
}

std::vector<std::reference_wrapper<Core::ECS::Actor>> Core::SceneSystem::Scene::FindActorsByTag(const std::string & p_tag) const
{
	std::vector<std::reference_wrapper<Core::ECS::Actor>> actors;

	for (auto actor : m_actors)
	{
		if (actor->GetTag() == p_tag)
			actors.push_back(std::ref(*actor));
	}

	return actors;
}

Core::ECS::Components::CCamera* Core::SceneSystem::Scene::FindMainCamera() const
{
	for (Core::ECS::Components::CCamera* camera : m_fastAccessComponents.cameras)
	{
		if (camera->owner.IsActive())
		{
			return camera;
		}
	}

	return nullptr;
}

void Core::SceneSystem::Scene::OnComponentAdded(ECS::Components::AComponent& p_compononent)
{
	if (auto result = dynamic_cast<ECS::Components::CModelRenderer*>(&p_compononent))
		m_fastAccessComponents.modelRenderers.push_back(result);

	if (auto result = dynamic_cast<ECS::Components::CCamera*>(&p_compononent))
		m_fastAccessComponents.cameras.push_back(result);

	if (auto result = dynamic_cast<ECS::Components::CLight*>(&p_compononent))
		m_fastAccessComponents.lights.push_back(result);

	if (auto result = dynamic_cast<ECS::Components::CPostProcessStack*>(&p_compononent))
		m_fastAccessComponents.postProcessStacks.push_back(result);

	if (auto result = dynamic_cast<ECS::Components::CReflectionProbe*>(&p_compononent))
		m_fastAccessComponents.reflectionProbes.push_back(result);
}

void Core::SceneSystem::Scene::OnComponentRemoved(ECS::Components::AComponent& p_compononent)
{
	if (auto result = dynamic_cast<ECS::Components::CModelRenderer*>(&p_compononent))
		m_fastAccessComponents.modelRenderers.erase(std::remove(m_fastAccessComponents.modelRenderers.begin(), m_fastAccessComponents.modelRenderers.end(), result), m_fastAccessComponents.modelRenderers.end());

	if (auto result = dynamic_cast<ECS::Components::CCamera*>(&p_compononent))
		m_fastAccessComponents.cameras.erase(std::remove(m_fastAccessComponents.cameras.begin(), m_fastAccessComponents.cameras.end(), result), m_fastAccessComponents.cameras.end());

	if (auto result = dynamic_cast<ECS::Components::CLight*>(&p_compononent))
		m_fastAccessComponents.lights.erase(std::remove(m_fastAccessComponents.lights.begin(), m_fastAccessComponents.lights.end(), result), m_fastAccessComponents.lights.end());

	if (auto result = dynamic_cast<ECS::Components::CPostProcessStack*>(&p_compononent))
		m_fastAccessComponents.postProcessStacks.erase(std::remove(m_fastAccessComponents.postProcessStacks.begin(), m_fastAccessComponents.postProcessStacks.end(), result), m_fastAccessComponents.postProcessStacks.end());

	if (auto result = dynamic_cast<ECS::Components::CReflectionProbe*>(&p_compononent))
		m_fastAccessComponents.reflectionProbes.erase(std::remove(m_fastAccessComponents.reflectionProbes.begin(), m_fastAccessComponents.reflectionProbes.end(), result), m_fastAccessComponents.reflectionProbes.end());
}

std::vector<Core::ECS::Actor*>& Core::SceneSystem::Scene::GetActors()
{
	return m_actors;
}

const Core::SceneSystem::Scene::FastAccessComponents& Core::SceneSystem::Scene::GetFastAccessComponents() const
{
	return m_fastAccessComponents;
}

void Core::SceneSystem::Scene::OnSerialize(tinyxml2::XMLDocument & p_doc, tinyxml2::XMLNode * p_root)
{
	tinyxml2::XMLNode* sceneNode = p_doc.NewElement("scene");
	p_root->InsertEndChild(sceneNode);

	tinyxml2::XMLNode* actorsNode = p_doc.NewElement("actors");
	sceneNode->InsertEndChild(actorsNode);

	for (auto& actor : m_actors)
	{
		actor->OnSerialize(p_doc, actorsNode);
	}
}

void Core::SceneSystem::Scene::OnDeserialize(tinyxml2::XMLDocument & p_doc, tinyxml2::XMLNode * p_root)
{
	tinyxml2::XMLNode* actorsRoot = p_root->FirstChildElement("actors");

	if (actorsRoot)
	{
		tinyxml2::XMLElement* currentActor = actorsRoot->FirstChildElement("actor");

		int64_t maxID = 1;

		while (currentActor)
		{
			auto& actor = CreateActor();
			actor.OnDeserialize(p_doc, currentActor);
			maxID = std::max(actor.GetID() + 1, maxID);
			currentActor = currentActor->NextSiblingElement("actor");
		}

		m_availableID = maxID;

		/* We recreate the hierarchy of the scene by attaching children to their parents */
		for (auto actor : m_actors)
		{
			if (actor->GetParentID() > 0)
			{
				if (auto found = FindActorByID(actor->GetParentID()); found)
					actor->SetParent(*found);
			}
		}
	}
}

void Core::SceneSystem::Scene::BuildSceneBvh()
{
	ZoneScoped;
	bvhService->Clear();
	int i = 0;
	std::vector<::Rendering::Geometry::bbox> bounds;
	std::vector<MeshInstance>meshInstances;
	std::vector<Core::Resources::Material*>matrialLists;
	std::vector<::Rendering::Resources::Mesh*>sceneMeshes;
	for (auto modelRenderer : m_fastAccessComponents.modelRenderers)
	{
		if (modelRenderer->owner.IsActive())
		{
			if (modelRenderer->owner.GetName() == "Skysphere") {
				continue;
			}
			
			auto model=modelRenderer->GetModel();
			auto mat=modelRenderer->owner.GetComponent<Core::ECS::Components::CMaterialRenderer>();
			if (model&&mat) {
				auto matrix = modelRenderer->owner.transform.GetWorldMatrix();	
				for (auto& m: model->GetMeshes()) {
					bool isTriMesh = m->GetPrimitiveMode() == ::Rendering::Settings::EPrimitiveMode::TRIANGLES;
					auto meshInstaceBox=m->GetBoundingBox().transform(matrix);
					if (isTriMesh&&meshInstaceBox.isValid()) {
						bounds.push_back(meshInstaceBox);

						//figure out  the matId
						int materialId = -1;
						if (isTriMesh) {
							auto meshMat=mat->GetMaterialAtIndex(m->GetMaterialIndex()[0]);
							for (int j = 0;j < matrialLists.size();j++) {
								if (matrialLists[j] == meshMat) {
									materialId = j;
									break;
								}
							}
							if (materialId == -1) {
								materialId = static_cast<int>(matrialLists.size());
								matrialLists.push_back(meshMat);
								bvhService->AddMaterial(meshMat);
							}
						}

						//figure out the meshId in the sceneMeshes if not find,push back the mesh to sceneMeshes
						int meshId = -1;
						for (int i = 0;i < sceneMeshes.size();i++) {
							if (sceneMeshes[i] == m) {
								meshId = i;
								break;
							}
						}
						if (meshId == -1) {
							meshId = static_cast<int>(sceneMeshes.size());
							sceneMeshes.push_back(m);
						}
						//push back the mesh instance
						MeshInstance meshInstance(modelRenderer->owner.GetID(),&modelRenderer->owner, meshId, matrix, materialId);
						meshInstances.push_back(meshInstance);
					}
				}
			}
		}
	}
	// Build BVH
	//
	// Every triangle mesh needs a BVH of its own as well, and whether it has one
	// depends on who made it: the topology batches call
	// ComputeBoundingSphereAndBox() per mesh while they are built, while a mesh read
	// from a file only gets the *model's* box. Building the missing ones here is what
	// keeps the path trace - and the picking that reads the same service - working
	// for both. A mesh with no triangles cannot have one and is left alone; the
	// service skips it below.
	for (::Rendering::Resources::Mesh* mesh : sceneMeshes) {
		if (mesh != nullptr
			&& mesh->GetPrimitiveMode() == ::Rendering::Settings::EPrimitiveMode::TRIANGLES
			&& mesh->GetBvh() == nullptr) {
			mesh->BuildBvh();
		}
	}
	if (meshInstances.size()>0) {
		bvhService->Process(bounds,sceneMeshes, meshInstances);	
	}
}

bool Core::SceneSystem::Scene::RayHit(const::Rendering::Geometry::Ray& ray, HitRes& outRes)
{
	return bvhService->RayHit(ray, outRes);
}

bool Core::SceneSystem::Scene::ClipRayHit(const::Rendering::Geometry::Ray& ray, const Maths::FVector4& plane, HitRes& outRes)
{
	return bvhService->ClipRayHit(ray,plane, outRes);
}

bool Core::SceneSystem::Scene::RayIteratorHit(const::Rendering::Geometry::Ray& ray,  HitRes& outRes)
{
	return bvhService->RayIteratorHit(ray, outRes);
}

bool Core::SceneSystem::Scene::PointPick(const Maths::FMatrix4& viewPortMatrix, int x, int y, float tolerance, PointPickRes& out)
{
	return bvhService->PointPick(viewPortMatrix,x,y,tolerance,out);
}

::Rendering::Geometry::Bvh* Core::SceneSystem::Scene::GetBvh()
{
	return bvhService->m_sceneBvh;
}

void Core::SceneSystem::Scene::computeBoundingBox()
{
	m_sceneBoundingBox = ::Rendering::Geometry::bbox();
	// The BVH is the cheap answer, but only when it holds bounds: a BVH that was built
	// while the scene had no triangle mesh in it keeps the default (empty) box, and
	// handing that out as the scene box gives every caller infinities to work with.
	if (bvhService->m_sceneBvh && bvhService->m_sceneBvh->m_bounds.isValid()) {
		m_sceneBoundingBox = bvhService->m_sceneBvh->Bounds();
	}
	else
	{
		for (auto modelRenderer : m_fastAccessComponents.modelRenderers)
		{
			if (modelRenderer->owner.IsActive())
			{
				auto model = modelRenderer->GetModel();
				if (model) {
					auto matrix = modelRenderer->owner.transform.GetWorldMatrix();
					for (auto& m : model->GetMeshes()) {
						m_sceneBoundingBox.grow(m->GetBoundingBox().transform(matrix));
					}
				}
			}
		}
	}
}
