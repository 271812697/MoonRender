#include "Sketcher/SketcherObjManager.h"
#include "Sketcher/SketcherObj.h"
#include "feature/SketcherFeature.h"
#include "editor/UI/TreeViewPanel/treeViewpanel.h"
#include "Core/Global/ServiceLocator.h"
#include <memory>
#include <vector>
namespace MOON {
	class SketcherObjManager::SketcherObjManagerInternal {
	public:
		SketcherObjManagerInternal(SketcherObjManager* s):self(s) {

		}
		~SketcherObjManagerInternal() {

		}
	private:
		friend SketcherObjManager;
		SketcherObjManager* self = nullptr;
		SketcherObj* currentSketcherObj = nullptr;
		SketcherFeature* currentSketcher = nullptr;
		std::vector<SketcherFeature*>sketchers;
	};
	SketcherObjManager& SketcherObjManager::instance() {
		static SketcherObjManager sketcherManager;
		return sketcherManager;
	}
	SketcherObjManager::~SketcherObjManager()
	{
		delete mInternal;
	}
	SketcherFeature* SketcherObjManager::CreateSketcherFeature()
	{
		SketcherFeature* ptr = new SketcherFeature("sketcher");
		mInternal->sketchers.push_back(ptr);;
		return ptr;
	}
	SketcherFeature* SketcherObjManager::GetCurrentActiveSketcherFeature()
	{
		return mInternal->currentSketcher;
	}
	SketcherFeature* SketcherObjManager::GetLastSketcherFeature()
	{
		if(mInternal->sketchers.size()>0)
		return mInternal->sketchers.back();
		return nullptr;
	}
	SketcherObj* SketcherObjManager::GetCurrentActiveSketcherObj()
	{
		auto feature = GetCurrentActiveSketcherFeature();
		if (feature) {
			return feature->getSketcherObj();
		}
		return nullptr;
	}
	void SketcherObjManager::setCurrentActiveSketcherFeature(SketcherFeature* obj)
	{
		for (int i = 0; i < mInternal->sketchers.size(); i++) {
			if (mInternal->sketchers[i] == obj) {
				mInternal->currentSketcher = obj;
			}
		}
	}
	void SketcherObjManager::addSketcherFeature(SketcherFeature* p_feature)
	{
		if (p_feature == nullptr) {
			return;
		}
		for (SketcherFeature* known : mInternal->sketchers) {
			if (known == p_feature) {
				return;
			}
		}
		mInternal->sketchers.push_back(p_feature);
	}
	void SketcherObjManager::removeSketcherFeature(SketcherFeature* p_feature)
	{
		if (p_feature == nullptr) {
			return;
		}
		for (int i = 0; i < static_cast<int>(mInternal->sketchers.size()); ++i) {
			if (mInternal->sketchers[i] == p_feature) {
				mInternal->sketchers.erase(mInternal->sketchers.begin() + i);
				break;
			}
		}
		if (mInternal->currentSketcher == p_feature) {
			mInternal->currentSketcher = nullptr;
		}
	}
	std::vector<SketcherObj*> SketcherObjManager::GetAllSketcherObjs()
	{
		std::vector<SketcherObj*>res;
		//for (int i = 0;i < mInternal->sketchers.size();i++) {
		//	res.push_back(mInternal->sketchers[i].get());
		//}
		return res;
	}

	SketcherObj* SketcherObjManager::CreateSketcherObj()
	{
		SketcherFeature* ptr = new SketcherFeature("sketcher");
		mInternal->sketchers.push_back(ptr);;
		return ptr->getSketcherObj();
	}
	SketcherObjManager::SketcherObjManager():mInternal(new SketcherObjManagerInternal(this))
	{
	}
}
