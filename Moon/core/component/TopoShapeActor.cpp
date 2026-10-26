#include "core/component/TopoShapeActor.h"
#include <iostream>
#include "renderer/SceneView.h"
#include <Core/ResourceManagement/MaterialManager.h>
#include <Core/ECS/Components/CMaterialRenderer.h>
#include <Core/ECS/Components/CModelRenderer.h>
#include "Core/ECS/Components/CBatchMeshTriangle.h"
#include "Core/ECS/Components/CBatchMeshLine.h"
#include "Core/ECS/Components/CBatchMeshPoint.h"
#include "Core/ResourceManagement/ModelManager.h"
#include "editor/View/sceneview/viewerwidget.h"
#include "Core/Global/ServiceLocator.h"
#include "renderer/Context.h"
#include "core/component/CTopoShape.h"
#include <Core/Global/ServiceLocator.h>
#include <Core/SceneSystem/Scene.h>
#include "TopoShape.h"
#include <Rendering/Data/Material.h>
#include "core/log.h"

	namespace
	{
		/** The CAD materials are assets under :Materials/; the manager owns them and loads one copy
		 * per file, so a shape looks them up instead of building its own. */
		::Core::Resources::Material* LoadCadMaterial(const std::string& p_key)
		{
			auto* material = Core::Global::ServiceLocator::Get<Core::ResourceManagement::MaterialManager>()
				.GetResource(p_key, true);
			if (material == nullptr)
			{
				CORE_WARN("[Material] the CAD material asset {} is missing", p_key);
			}
			return material;
		}
	}

namespace MOON {

	TopoActor::TopoActor(const std::string&p_name , const std::string& p_tag, bool p_playing, bool addToTree) :
		Actor(0, p_name, p_tag, p_playing )
	{
		
		auto scene = GetService(Editor::Core::Context).sceneManager.GetCurrentScene();
		m_scene = scene;
		SetID(scene->GetAvailableID());
		scene->AddActor(this);
		Core::ECS::Actor& faceChild=scene->CreateActor("AllFaces");
		Core::ECS::Actor& edgeChild = scene->CreateActor("AllEdges");
		Core::ECS::Actor& vertexChild = scene->CreateActor("AllVertices");
		// The Face/Edge/Vertex children are render anchors holding the batched
		// meshes. They stay visible in the TreeView so the user can select them to
		// hide or show the whole face/edge/vertex layer at once, or fit the camera
		// to them. The topology actors created by CTopoShape form the per-solid tree.
		faceChild.SetTag("TopoRender");
		edgeChild.SetTag("TopoRender");
		vertexChild.SetTag("TopoRender");
		faceChild.SetParent(*this);
		edgeChild.SetParent(*this);
		vertexChild.SetParent(*this);
		AddComponent<Core::ECS::Components::CTopoShape>();
		topoShape = &GetComponent<Core::ECS::Components::CTopoShape>()->GetTopoShape();
		faceChild.AddComponent<Core::ECS::Components::CModelRenderer>();
		faceChild.AddComponent<Core::ECS::Components::CMaterialRenderer>();
		faceChild.AddComponent<Core::ECS::Components::CBatchMeshTriangle>();
		edgeChild.AddComponent<Core::ECS::Components::CModelRenderer>();
		edgeChild.AddComponent<Core::ECS::Components::CMaterialRenderer>();
		edgeChild.AddComponent<Core::ECS::Components::CBatchMeshLine>();
		vertexChild.AddComponent<Core::ECS::Components::CModelRenderer>();
		vertexChild.AddComponent<Core::ECS::Components::CMaterialRenderer>();
		vertexChild.AddComponent<Core::ECS::Components::CBatchMeshPoint>();
		
		
		auto faceModel = new ::Rendering::Resources::Model(p_name + std::string("_faceModel")+std::to_string(this->GetID()));
		faceChild.GetComponent<Core::ECS::Components::CModelRenderer>()->SetModel(faceModel);
		GetService(Core::ResourceManagement::ModelManager).RegisterResource(p_name + std::string("_faceModel") + std::to_string(this->GetID()), faceModel);
		auto edgeModel = new ::Rendering::Resources::Model(p_name + std::string("_edgeModel") + std::to_string(this->GetID()));
		edgeChild.GetComponent<Core::ECS::Components::CModelRenderer>()->SetModel(edgeModel);
		GetService(Core::ResourceManagement::ModelManager).RegisterResource(p_name + std::string("_edgeModel") + std::to_string(this->GetID()), edgeModel);
		auto vertexModel = new ::Rendering::Resources::Model(p_name + std::string("_vertexModel") + std::to_string(this->GetID()));
		vertexChild.GetComponent<Core::ECS::Components::CModelRenderer>()->SetModel(vertexModel);
		GetService(Core::ResourceManagement::ModelManager).RegisterResource(p_name + std::string("_vertexModel") + std::to_string(this->GetID()), vertexModel);

		// The four CAD materials are assets (CADFace / CADFaceTransparent / CADLine / CADPoint
		// under :Materials/), so the resource manager already holds the one instance of each that
		// the document shares: every shape just draws with its own instance of them.
		auto& faceRenderer = *faceChild.GetComponent<Core::ECS::Components::CMaterialRenderer>();
		auto& edgeRenderer = *edgeChild.GetComponent<Core::ECS::Components::CMaterialRenderer>();
		auto& vertexRenderer = *vertexChild.GetComponent<Core::ECS::Components::CMaterialRenderer>();

		if (auto* faceMaterial = LoadCadMaterial(":Materials/CADFace.mat"))
		{
			faceRenderer.SetMaterialInstanceAtIndex(0, faceMaterial->CreateInstance());
		}
		if (auto* transparentMaterial = LoadCadMaterial(":Materials/CADFaceTransparent.mat"))
		{
			faceRenderer.SetMaterialInstanceAtIndex(1, transparentMaterial->CreateInstance());
		}
		if (auto* lineMaterial = LoadCadMaterial(":Materials/CADLine.mat"))
		{
			edgeRenderer.SetMaterialInstanceAtIndex(0, lineMaterial->CreateInstance());
		}
		if (auto* pointMaterial = LoadCadMaterial(":Materials/CADPoint.mat"))
		{
			vertexRenderer.SetMaterialInstanceAtIndex(0, pointMaterial->CreateInstance());
		}

		faceRenderer.UpdateMaterialList();
		edgeRenderer.UpdateMaterialList();
		vertexRenderer.UpdateMaterialList();
		// The colour the shape carries goes on the material now that there is one: the
		// component is made before these render anchors are, so a colour that arrived
		// earlier (a feature read out of a document) had nowhere to go until here.
		if (auto* topoShape = GetComponent<Core::ECS::Components::CTopoShape>()) {
			topoShape->ApplyColor();
		}
		if (addToTree) {
			GetViewerWidget.addActorToTreeView(this);
		}
	}

	void TopoActor::ClearModel()
	{
		GetChild("AllFaces")->GetComponent<Core::ECS::Components::CModelRenderer>()->GetModel()->ClearMeshes();
		GetChild("AllEdges")->GetComponent<Core::ECS::Components::CModelRenderer>()->GetModel()->ClearMeshes();
		GetChild("AllVertices")->GetComponent<Core::ECS::Components::CModelRenderer>()->GetModel()->ClearMeshes();
	}

	TopoActor::~TopoActor()
	{
		//do we need remove ourself from scene?
	}

	void TopoActor::OnSerialize(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_actorsRoot)
	{
		Actor::OnSerialize(p_doc, p_actorsRoot);
	}

	void TopoActor::OnDeserialize(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_actorsRoot)
	{
		Actor::OnDeserialize(p_doc, p_actorsRoot);
	}

	void TopoActor::RemoveFromScene()
	{
		// The row the tree shows this actor as goes with it: a row outlives its actor
		// otherwise, and whoever reads the actor off such a row - renaming it, moving it,
		// deleting the chain it belongs to - would be reading freed memory.
		GetViewerWidget.removeActorFromTreeView(this);
		if (m_scene) {
			m_scene->RemoveActor(this);
		}		
	}
	Part::TopoShape& TopoActor::GetTopoShape()
	{
		return *topoShape;
	}
	void TopoActor::setTopoShape(Part::TopoShape shape)
	{
		topoShape->setShape(shape);;
		GetComponent<Core::ECS::Components::CTopoShape>()->discretizationShape();
	}
}