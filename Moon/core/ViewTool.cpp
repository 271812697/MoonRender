#include "core/ViewTool.h"
#include "core/SelectionManager.h"
#include "TopoShape.h"
#include "Core/Global/ServiceLocator.h"
#include "core/component/CTopoShape.h"
#include "core/component/TopoShapeActor.h"
#include "renderer/SceneView.h"
#include "feature/Feature.h"
namespace MOON {
	Core::ECS::Actor* ViewTool::getLastestActorSelected()
	{
		std::vector<SelectID> selectIds = SelectionManager::instance().getSelect();
		if (!selectIds.empty()) {
			auto& view = GetService(Editor::Panels::SceneView);
			auto scene = view.GetScene();
			auto actor = scene->FindActorByID(selectIds.back());
			return actor;
		}
		return nullptr;
	}
	bool ViewTool::getSelectedTopoShape(std::vector<Part::TopoShape>& topo)
	{
		Core::ECS::Actor* actor =getLastestActorSelected();
		if (!actor) {
			return false;
		}
		//in fact indexName is not right to use to get the ref shape!
		for (Core::ECS::Actor* cur = actor->HasParent() ? actor->GetParent() : nullptr;
			cur; cur = cur->HasParent() ? cur->GetParent() : nullptr) {
			if (cur->HasComponent("CTopoShape")) {
				auto topoComp = cur->GetComponent<::Core::ECS::Components::CTopoShape>();
				std::string idString = actor->GetName().substr(5);
				std::string type = actor->GetName().substr(0, 4);
				int childId = std::stoi(idString);
				topo.push_back(topoComp->GetTopoShape());
				topo.push_back(type == "face" ? topoComp->GetTopoFace(childId) : topoComp->GetTopoEdge(childId));
				return true;
			}

		}
		return false;
	}
	Feature* ViewTool::getSelectedFeature()
	{
		Core::ECS::Actor* actor = getLastestActorSelected();
		if (!actor) {
			return nullptr;
		}
		Feature* f = dynamic_cast<Feature*>(actor);
		return f;
	}
	bool ViewTool::getSelectedBasedFeature(Feature*&f,std::vector<std::string>&subValues)
	{
		//if use the method,we pretend that the selectids is from a Feature.
		Core::ECS::Actor* actor = getLastestActorSelected();
		if (!actor) {
			return false;
		}
		std::string firstReference;
		if (!getActorBasedFeature(actor, f, firstReference)) {
			return false;
		}
		std::vector<SelectID> selectIds = SelectionManager::instance().getSelect();
		subValues.clear();
		subValues.reserve(selectIds.size());
		auto& view = GetService(Editor::Panels::SceneView);
		auto scene = view.GetScene();
		for (int i = 0;i < selectIds.size();i++) {
			auto tempActor = scene->FindActorByID(selectIds[i]);
			if (tempActor) {
				subValues.emplace_back(tempActor->GetName());
			}
		}
		return true;
	}
	bool ViewTool::getActorBasedFeature(
		Core::ECS::Actor* actor,
		Feature*& f,
		std::string& subValue)
	{
		if (!actor) {
			return false;
		}
		// The topology leaves (Face_*/Edge_*) can live at arbitrary depth below
		// the Feature (Solid/Shell groups), so walk up the whole chain instead
		// of assuming the Feature is the grandparent.
		for (Core::ECS::Actor* cur = actor->HasParent() ? actor->GetParent() : nullptr;
			cur; cur = cur->HasParent() ? cur->GetParent() : nullptr) {
			Feature* feature = dynamic_cast<Feature*>(cur);
			if (feature) {
				f = feature;
				subValue = actor->GetName();
				return true;
			}
		}
		return false;
	}
	 Core::ECS::Actor* ViewTool::createTopoActor(const Part::TopoShape& topoShape, const char* name )
	{

		auto topoActor = new TopoActor( std::string(name), "TopoShape", false);
		const auto& topoComp = topoActor->GetComponent<Core::ECS::Components::CTopoShape>();
		Part::TopoShape& topo = topoComp->GetTopoShape();
		topo = topoShape;
		topoComp->discretizationShape();
		return topoActor;
	}
}
