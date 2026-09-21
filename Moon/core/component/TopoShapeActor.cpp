#include "core/component/TopoShapeActor.h"
#include "renderer/SceneView.h"
#include <Core/ResourceManagement/MaterialManager.h>
#include <Core/ECS/Components/CMaterialRenderer.h>
#include <Core/ECS/Components/CModelRenderer.h>
#include "Core/ECS/Components/CBatchMeshTriangle.h"
#include "Core/ECS/Components/CBatchMeshLine.h"
#include "Core/ResourceManagement/ModelManager.h"
#include "editor/View/sceneview/viewerwidget.h"
#include "Core/Global/ServiceLocator.h"
#include "renderer/Context.h"
#include "core/component/CTopoShape.h"
#include <Core/Global/ServiceLocator.h>
#include <Core/SceneSystem/Scene.h>
#include "TopoShape.h"

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
		// The Face/Edge children are render anchors holding the batched meshes.
		// They stay visible in the TreeView so the user can select them to hide
		// or show the whole face/edge layer at once, or fit the camera to them.
		// The topology actors created by CTopoShape form the per-solid tree.
		faceChild.SetTag("TopoRender");
		edgeChild.SetTag("TopoRender");
		faceChild.SetParent(*this);
		edgeChild.SetParent(*this);
		AddComponent<Core::ECS::Components::CTopoShape>();
		topoShape = &GetComponent<Core::ECS::Components::CTopoShape>()->GetTopoShape();
		faceChild.AddComponent<Core::ECS::Components::CModelRenderer>();
		faceChild.AddComponent<Core::ECS::Components::CMaterialRenderer>();
		faceChild.AddComponent<Core::ECS::Components::CBatchMeshTriangle>();
		edgeChild.AddComponent<Core::ECS::Components::CModelRenderer>();
		edgeChild.AddComponent<Core::ECS::Components::CMaterialRenderer>();
		edgeChild.AddComponent<Core::ECS::Components::CBatchMeshLine>();
		
		
		auto faceModel = new ::Rendering::Resources::Model(p_name + std::string("_faceModel")+std::to_string(this->GetID()));
		faceChild.GetComponent<Core::ECS::Components::CModelRenderer>()->SetModel(faceModel);
		GetService(Core::ResourceManagement::ModelManager).RegisterResource(p_name + std::string("_faceModel") + std::to_string(this->GetID()), faceModel);
		auto edgeModel = new ::Rendering::Resources::Model(p_name + std::string("_edgeModel") + std::to_string(this->GetID()));
		edgeChild.GetComponent<Core::ECS::Components::CModelRenderer>()->SetModel(edgeModel);
		GetService(Core::ResourceManagement::ModelManager).RegisterResource(p_name + std::string("_edgeModel") + std::to_string(this->GetID()), edgeModel);

		Core::Resources::Material* lineMat = new Core::Resources::Material();
		Core::Global::ServiceLocator::Get<Core::ResourceManagement::MaterialManager>().RegisterResource(p_name + std::to_string(this->GetID()) + "_linemat",lineMat);
		Core::Resources::Material* faceMat = new Core::Resources::Material();
		Core::Global::ServiceLocator::Get<Core::ResourceManagement::MaterialManager>().RegisterResource(p_name + std::to_string(this->GetID()) + "_facemat", faceMat);
		Core::Resources::Material* faceTransparentMat = new Core::Resources::Material();
		Core::Global::ServiceLocator::Get<Core::ResourceManagement::MaterialManager>().RegisterResource(p_name + std::to_string(this->GetID()) + "_faceTransparentMat", faceTransparentMat);
		faceChild.GetComponent<Core::ECS::Components::CMaterialRenderer>()->SetMaterialAtIndex(0, *faceMat);
		faceChild.GetComponent<Core::ECS::Components::CMaterialRenderer>()->SetMaterialAtIndex(1, *faceTransparentMat);		
		edgeChild.GetComponent<Core::ECS::Components::CMaterialRenderer>()->SetMaterialAtIndex(0, *lineMat);
		faceChild.GetComponent<Core::ECS::Components::CMaterialRenderer>()->UpdateMaterialList();
		edgeChild.GetComponent<Core::ECS::Components::CMaterialRenderer>()->UpdateMaterialList();
		{
			auto& renderer=GetSceneView.GetRenderer();;
			{
				faceMat->SetBackfaceCulling(false);
				faceMat->SetCastShadows(false);
				faceMat->SetReceiveShadows(false);
				// Push the faces back in depth (glPolygonOffset, slope scaled) so
				// edge lines that are coplanar with / close to their faces (concave
				// folds, small dihedral angles) stay visible on top. Note: with
				// the reversed-Z depth range the driver applies a NEGATIVE offset
				// (see Driver::SetPipelineState), keep the signs consistent.
				faceMat->SetPolygonOffsetFill(true);
				//tempMat->SetBlendable(true);
				//tempMat->SetDepthWriting(false);
				faceMat->SetShader(Core::Global::ServiceLocator::Get<Editor::Core::Context>().shaderManager[":Shaders\\GeomertySurface.ovfx"]);
				faceMat->SetProperty("u_Albedo",Maths::FVector4(1,1,1,1));
				faceMat->SetProperty("u_AlphaClippingThreshold", 0.0f);
				faceMat->SetProperty("u_Roughness", 0.25f);
				faceMat->SetProperty("u_Metallic", 0.25f);
				// Emission
				faceMat->SetProperty("u_EmissiveIntensity", 1.0f);
				faceMat->SetProperty("u_EmissiveColor", Maths::FVector3{ 0.0f, 0.0f, 0.0f });

				faceMat->TrySetProperty("_IrradianceCube", renderer.GetIrradianceCube());
				faceMat->TrySetProperty("_PrefilterCube", renderer.GetPrefilterCube());
				faceMat->TrySetProperty("_BRDFLut", renderer.GetBrdfTexture());


				faceTransparentMat->SetBackfaceCulling(false);
				faceTransparentMat->SetCastShadows(false);
				faceTransparentMat->SetReceiveShadows(false);
				//tempMat->SetBlendable(true);
				//tempMat->SetDepthWriting(false);
				faceTransparentMat->SetShader(Core::Global::ServiceLocator::Get<Editor::Core::Context>().shaderManager[":Shaders\\GeomertySurface.ovfx"]);
				faceTransparentMat->SetProperty("u_Albedo", Maths::FVector4(1, 0, 0, 0.5));
				faceTransparentMat->SetProperty("u_AlphaClippingThreshold", 0.0f);
				faceTransparentMat->SetProperty("u_Roughness", 0.25f);
				faceTransparentMat->SetProperty("u_Metallic", 0.25f);
				// Emission
				faceTransparentMat->SetProperty("u_EmissiveIntensity", 1.0f);
				faceTransparentMat->SetProperty("u_EmissiveColor", Maths::FVector3{ 0.0f, 0.0f, 0.0f });

				faceTransparentMat->TrySetProperty("_IrradianceCube", renderer.GetIrradianceCube());
				faceTransparentMat->TrySetProperty("_PrefilterCube", renderer.GetPrefilterCube());
				faceTransparentMat->TrySetProperty("_BRDFLut", renderer.GetBrdfTexture());
				faceTransparentMat->SetTransparent(true);
				faceTransparentMat->SetDepthWriting(true);
			}
			{
				lineMat->SetShader(Core::Global::ServiceLocator::Get<Editor::Core::Context>().shaderManager[":Shaders\\GeomertyLine.ovfx"]);
				lineMat->SetBackfaceCulling(false);
				lineMat->SetCastShadows(false);
				lineMat->SetReceiveShadows(false);
				// Edge lines are coplanar with their faces: never write depth so
				// they don't z-fight with the faces (the line pass uses LEQUAL).
				lineMat->SetDepthWriting(false);
				lineMat->SetLineWidth(1.5);
				lineMat->AddFeature("CLIP_PLANE");
				
			}

		}
		if (addToTree) {
			GetViewerWidget.addActorToTreeView(this);
		}

	}

	void TopoActor::ClearModel()
	{
		GetChild("AllFaces")->GetComponent<Core::ECS::Components::CModelRenderer>()->GetModel()->ClearMeshes();
		GetChild("AllEdges")->GetComponent<Core::ECS::Components::CModelRenderer>()->GetModel()->ClearMeshes();
	}

	TopoActor::~TopoActor()
	{
		//m_scene->

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
