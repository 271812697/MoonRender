#include "core/SelectionManager.h"
#include "renderer/SceneView.h"
#include "core/Global/ServiceLocator.h"
#include "core/component/CTopoShape.h"
#include <unordered_set>

namespace MOON {
	namespace
	{
		// The topology leaves (Face_*/Edge_*) can live at arbitrary depth below
		// the topo actor (Solid/Shell groups), so resolve the selection by the
		// leaf name and by walking up to the CTopoShape owner instead of
		// relying on the old Face/Edge render-children hierarchy.
		struct TopoSelectionInfo
		{
			::Core::ECS::Components::CTopoShape* topo = nullptr;
			int childId = -1;
			bool isFace = false;
			bool isEdge = false;
			bool isVertex = false;
		};
		TopoSelectionInfo ResolveTopoSelection(::Core::ECS::Actor* actor)
		{
			TopoSelectionInfo info;
			if (!actor) {
				return info;
			}
			const std::string& name = actor->GetName();
			if (name.rfind("Face_", 0) == 0) {
				info.isFace = true;
			}
			else if (name.rfind("Edge_", 0) == 0) {
				info.isEdge = true;
			}
			else if (name.rfind("Vertex_", 0) == 0) {
				info.isVertex = true;
			}
			else {
				return info;
			}
			try {
				// "Face_"/"Edge_" carry one digit prefix less than "Vertex_".
				info.childId = std::stoi(name.substr(info.isVertex ? 7 : 5));
			}
			catch (...) {
				return info;
			}
			for (::Core::ECS::Actor* cur = actor->HasParent() ? actor->GetParent() : nullptr;
				cur; cur = cur->HasParent() ? cur->GetParent() : nullptr) {
				if (cur->HasComponent("CTopoShape")) {
					info.topo = cur->GetComponent<::Core::ECS::Components::CTopoShape>();
					break;
				}
			}
			return info;
		}
	}

	struct SelectIDHash
	{
		size_t operator()(const SelectID& id) const noexcept
		{
			return std::hash<size_t>{}(id.actorId);
		}
	};
	class SelectionManager::Internal 
	{
	public:
		Internal(SelectionManager* s):self(s){
			mSelectMode=OverrideSelect;
		}
		~Internal() {
		}
		void clearSelectEffect(const std::unordered_set<SelectID, SelectIDHash>& clearList) {
		
			std::unordered_map<::Core::ECS::Components::CTopoShape*, int>selectFaceMap;
			std::unordered_map<::Core::ECS::Components::CTopoShape*, int>selectEdgeMap;
			std::unordered_map<::Core::ECS::Components::CTopoShape*, int>selectVertexMap;
			for (auto& id : clearList) {
				auto actor = GetMainScene->FindActorByID(id);
				if (actor) {
					auto info = ResolveTopoSelection(actor);
					if (info.topo && info.childId != -1) {
						if (info.isFace) {
							selectFaceMap[info.topo] = 1;
						}
						else if (info.isEdge) {
							selectEdgeMap[info.topo] = 1;
						}
						else if (info.isVertex) {
							selectVertexMap[info.topo] = 1;
						}
					}
				}
			}
			for (auto& it : selectFaceMap) {
				it.first->selectChildFaces({});
			}
			for (auto& it : selectEdgeMap) {
				it.first->clearSelectLines();
			}
			for (auto& it : selectVertexMap) {
				it.first->clearSelectVertex();
			}
		}
	private:
		friend SelectionManager;
		SelectionManager* self = nullptr;
		SelectID preSelect;

		std::unordered_set<SelectID, SelectIDHash>selectIDs;
		SelectMode mSelectMode;
	};
	SelectionManager& SelectionManager::instance()
	{
		static SelectionManager manager;
		return manager;
	}
	void SelectionManager::setPreselect(SelectID id)
	{
		if (mInternal->preSelect != id) {
			clearPreselect();
			mInternal->preSelect = id;
			if (mInternal->preSelect.isValid()) {
				auto actor = GetMainScene->FindActorByID(mInternal->preSelect);
				if (actor) {
					auto info = ResolveTopoSelection(actor);
					if (info.topo && info.childId != -1) {
						if (info.isFace) {
							info.topo->hoverChild(info.childId);
						}
						else if (info.isEdge) {
							info.topo->hoverChildLine(info.childId);
						}
						else if (info.isVertex) {
							info.topo->hoverChildVertex(info.childId);
						}
					}
				}
			}
		}
	}
	void SelectionManager::select(const std::vector<SelectID>& selectIdLists)
	{
		std::unordered_set<SelectID, SelectIDHash> beforeSelect = mInternal->selectIDs;
		//this means select nothing we need clear
		if (mInternal->mSelectMode == SelectMode::OverrideSelect&& selectIdLists.size()==0) {
			mInternal->clearSelectEffect(mInternal->selectIDs);
			mInternal->selectIDs.clear();
			return;
		}
		
		if (mInternal->mSelectMode==SelectMode::OverrideSelect) {
			mInternal->selectIDs.clear();
			mInternal->selectIDs.insert(selectIdLists.begin(), selectIdLists.end());
		}
		else if(mInternal->mSelectMode == SelectMode::AddSelect)
		{
			if (selectIdLists.size() > 0) {
				mInternal->selectIDs.insert( selectIdLists.begin(), selectIdLists.end());
			}
		}
		if (mInternal->selectIDs != beforeSelect)
		{
			mInternal->clearSelectEffect(beforeSelect);
			std::unordered_map<::Core::ECS::Components::CTopoShape*, std::vector<int>>selectFaceMap;
			std::unordered_map<::Core::ECS::Components::CTopoShape*, std::vector<int>>selectEdgeMap;
			std::unordered_map<::Core::ECS::Components::CTopoShape*, std::vector<int>>selectVertexMap;
			for (auto& id:mInternal->selectIDs) {
				auto actor = GetMainScene->FindActorByID(id);
				if (actor) {
					auto info = ResolveTopoSelection(actor);
					if (info.topo && info.childId != -1) {
						if (info.isFace) {
							selectFaceMap[info.topo].push_back(info.childId);
						}
						else if (info.isEdge) {
							selectEdgeMap[info.topo].push_back(info.childId);
						}
						else if (info.isVertex) {
							selectVertexMap[info.topo].push_back(info.childId);
						}
					}
				}
			}
			for(auto&it:selectFaceMap){
				it.first->selectChildFaces(it.second);
			}
			for (auto& it : selectEdgeMap) {
				it.first->selectChildLines(it.second);
			}
			for (auto& it : selectVertexMap) {
				it.first->selectChildVertex(it.second);
			}
			InvokeEvent(SelectAny);
		}
	}
	void SelectionManager::clearSelect()
	{
		mInternal->selectIDs.clear();
	}
	void SelectionManager::clearPreselect()
	{
		if (mInternal->preSelect.isValid()) {
			auto actor=GetMainScene->FindActorByID(mInternal->preSelect);
			if (actor) {
				auto info = ResolveTopoSelection(actor);
				if (info.topo) {
					if (info.isFace) {
						info.topo->clearHover();
					}
					else if (info.isEdge) {
						info.topo->clearHoverLine();
					}
					else if (info.isVertex) {
						info.topo->clearHoverVertex();
					}
				}
			}
			mInternal->preSelect.reset();
		}
	}
	SelectID SelectionManager::getPreselect()
	{
		return mInternal->preSelect;
	}
	std::vector<SelectID> SelectionManager::getSelect()
	{
		return std::vector<SelectID>{ mInternal->selectIDs.begin(), mInternal->selectIDs.end() };
	}
	void SelectionManager::setSelectMode(SelectMode mode)
	{
		mInternal->mSelectMode = mode;
	}
	SelectionManager::~SelectionManager()
	{
		if (mInternal) {
			delete mInternal;
		}
	}
	SelectionManager::SelectionManager():mInternal(new Internal(this))
	{
	}
}
