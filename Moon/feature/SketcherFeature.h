#pragma once
#include "feature/Feature.h"
namespace MOON { 

	class SketcherObj;
	class SketcherFeature :public ProfileFeature {
	public:
		SketcherFeature(const std::string& p_name);
		SketcherObj* getSketcherObj();
		virtual ~SketcherFeature() override;
		virtual bool execute();
		/** A sketch hands on a wire: there is nothing to merge, and the names of its
		 * elements are what the features above reference, so it is left alone. */
		virtual bool isRefineActive() const override { return false; }
	private:
		class Internal;
		Internal* mInternal = nullptr;
	};
}
