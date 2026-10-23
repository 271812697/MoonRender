#include "Interactive/Widgets/RotateCenter.h"
#include "Interactive/Im3DRenderer.h"
#include "renderer/SceneView.h"
#include "Interactive/Interactive/ExecuteCommand.h"
#include "Interactive/Interactive/RenderWindowInteractor.h"
#include <ThirdParty/imgui/imgui/imgui.h>
#include <Core/ECS/Components/CBatchMeshTriangle.h>
#include <core/ECS/Components/CBatchMeshLine.h>
#include "core/component/CTopoShape.h"
#include "Core/Global/ServiceLocator.h"
#include "editor/UI/TreeViewPanel/treeViewpanel.h"
#include "core/SelectionManager.h"
namespace MOON {
	int eid = -1;
	uint64_t actorId = 0;
	bool drawCenter = false;
	bool drawRect = false;
	float sx;
	float sy;
	float ex;
	float ey;
	std::vector<Eigen::Vector3f> lineSeg;
	RotateCenter::RotateCenter(const std::string& name) :EventWidget(name)
	{
	
	}
	RotateCenter::~RotateCenter()
	{

	}
	void RotateCenter::onUpdate()
	{
		if (drawCenter) {
			auto rc = m_sceneView->GetRoaterCenter();
			Eigen::Vector3f center = { rc.x,rc.y,rc.z };
			renderer->drawOneMesh(
				center,
				Eigen::Matrix3f::Identity(),
				Eigen::Vector3f{ 0.1f,0.1f,0.1f },
				"CoordAxis");
		}
		for (int i = 0;i < lineSeg.size();i ++) {
			renderer->drawPoint(lineSeg[i], 7);
		}
		if (lineSeg.size() > 0) {
			renderer->drawLineList(lineSeg,3.0f, Eigen::Vector4<uint8_t>(255,255,255,255));
		}
	}
	void RotateCenter::onLeftMousePressed()
	{
		drawRect = true;
		auto it = m_sceneView->getInutState().GetMousePosition();
		sx = it.first;
		sy = it.second;

		auto pickingResult = m_sceneView->GetPickResult();
		if (pickingResult.has_value())
		{
			if (const auto pval = std::get_if<Tools::Utils::OptRef<::Core::ECS::Actor>>(&pickingResult.value()))
			{
				auto actor = *pval;
				if (actor) {	
					GetTreeView.highlightByActor(&actor.value());
					MOON::SelectionManager::instance().select({ actor.value().GetID() });
				}
				else
				{
					MOON::SelectionManager::instance().select({});
					GetTreeView.clearHighlight();
				}
			}
			else
			{
				MOON::SelectionManager::instance().select({});
				GetTreeView.clearHighlight();
			}
		}
		else
		{
			MOON::SelectionManager::instance().select({});
			GetTreeView.clearHighlight();
		}
	}
	void RotateCenter::onLeftMouseReleased()
	{
		drawRect = false;
	/*	
	    auto [w, h] = m_sceneView->GetSafeSize();
		float su = 2 * (sx) / (float)w - 1;
		float sv = 2 * (h - sy) / (float)h - 1;
		float eu = 2 * (ex) / (float)w - 1;
		float ev = 2 * (h - ey) / (float)h - 1;

		auto res = m_sceneView->GetScene()->GetBvhService()->RectPick(m_sceneView->GetCamera()->GetViewProjectionMatrix(),
			std::min(su, eu), std::min(sv, ev), std::max(su, eu), std::max(sv, ev));
		if (res.size() > 0) {
			std::unordered_map<uint64_t, std::vector<int>>actorPointMap;
			for (auto& r : res) {
				actorPointMap[r.actorId].push_back(r.childId);
			}
			for (auto& it : actorPointMap) {
				auto actor = m_sceneView->GetScene()->FindActorByID(it.first);
				if (actor) {
					if (actor->HasComponent("CBatchMeshTriangle")) {
						auto colorBar = actor->GetComponent<::Core::ECS::Components::CBatchMeshTriangle>();
						colorBar->SetCandidatesIndex(it.second);
						if (colorBar) {
							colorBar->SetColor(Maths::FVector4{ 1.0f,0.5019f,0.0f,1.0f });
						}
						auto topoComp = actor->GetParent()->GetComponent<::Core::ECS::Components::CTopoShape>();
						topoComp->setChildsMeshTransParent({ it.second });
					}
				}
			}
		}*/
	}
	void RotateCenter::onRightMousePressed()
	{		
		drawCenter = true;
	}
	void RotateCenter::onRightMouseReleased()
	{
		drawCenter = false;
	}

	void RotateCenter::onMouseMove()
	{
		auto pickingResult=m_sceneView->GetPickResult();
		if (pickingResult.has_value())
		{
			if (const auto pval = std::get_if<Tools::Utils::OptRef<::Core::ECS::Actor>>(&pickingResult.value()))
			{
				auto actor = *pval;
				if (actor) {
					MOON::SelectionManager::instance().setPreselect({ actor.value().GetID() });
				}
				else
				{
					MOON::SelectionManager::instance().clearPreselect();
				}
			}
			else
			{
				MOON::SelectionManager::instance().clearPreselect();
			}
		}
		else
		{
			MOON::SelectionManager::instance().clearPreselect();
		}
		auto it = m_sceneView->getInutState().GetMousePosition();
		ex = it.first;
		ey = it.second;
		//auto ray=m_sceneView->GetMouseRay();
		//::Core::SceneSystem::HitRes res;
		//if (m_sceneView->GetScene()->RayHit(ray, res)) {
		//	int id=round(res.hitUv.x);
		//	if (id != eid) {
		//		actorId =res.actorId;
		//		eid = id;
		//		auto actor = m_sceneView->GetScene()->FindActorByID(actorId);
		//		if (actor) {
		//			if (actor->HasComponent("CTopoShape")) {
		//				auto topoComp = actor->GetComponent<::Core::ECS::Components::CTopoShape>();
		//				topoComp->hoverChild(eid);
		//				GetTreeView.highlightByActor(actor->GetChildren()[eid]);
		//				SelectionManager::instance().setPreselect({ actor->GetChildren()[eid]->GetID() });
		//			}
		//		}
		//	}
		//}
		//else
		//{
		//	if (eid != -1) {
		//		eid = -1;
		//		SelectionManager::instance().clearPreselect();
		//		GetTreeView.clearHighlight();
		//		lineSeg.clear();
		//	}
		//}
		//auto [w, h] = m_sceneView->GetSafeSize();
		//Maths::FMatrix4 viewPortMatrix=Maths::FMatrix4::Scaling({ w / 2.0f,h / 2.0f,1.0f })*Maths::FMatrix4::Translation({1,1,0})*m_sceneView->GetCamera()->GetViewProjectionMatrix();
		//::Core::SceneSystem::PointPickRes out;
		//if (m_sceneView->GetScene()->PointPick(viewPortMatrix, ex, h - ey, 3.0f, out)) {
		//	static int subLineId = -1;
		//	int id = out.subMeshId;
		//	if (id != subLineId) 
		//	{
		//		subLineId = id;
		//		auto actor = m_sceneView->GetScene()->FindActorByID(out.actorId);
		//		if (actor) {
		//			if (actor->HasComponent("CBatchMeshLine")) {
		//				auto colorBar = actor->GetComponent<::Core::ECS::Components::CBatchMeshLine>();
		//				if (colorBar) {
		//					auto vertexArray = colorBar->getLineSeg(subLineId);
		//					lineSeg.clear();
		//					lineSeg.reserve(vertexArray.size());
		//					for (auto v : vertexArray) {
		//						lineSeg.push_back(Eigen::Vector3f(v.x, v.y, v.z));;
		//					}
		//				}
		//			}
		//		}
		//	}
		//}
	}
	void RotateCenter::onKeyPress(const std::string& key)
	{
		if (key == "CONTROL_L") {
			GetSelection.setSelectMode(SelectMode::AddSelect);
		}
		
	}
	void RotateCenter::onKeyRelease(const std::string& key)
	{
		if (key == "CONTROL_L") {
			GetSelection.setSelectMode(SelectMode::OverrideSelect);
		}
	}
}