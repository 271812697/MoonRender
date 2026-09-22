
#include "feature/FeatureBody.h"
#include "SketcherFeature.h"
#include "feature/FeatureBaseProfile.h"
#include "core/log.h"

namespace MOON {
	class FeatureBody::Internal {
	public:
		Internal(FeatureBody* f):self(f) {}
		~Internal() {}
	private:
		friend FeatureBody;
		FeatureBody* self = nullptr;
		std::vector<Feature*>featureList;
		/** How deep the recompute propagation currently is. */
		int populateDepth = 0;
	};
	FeatureBody::FeatureBody(const std::string& p_name) :mInternal(new Internal(this))
	{

	}
	FeatureBody::~FeatureBody()
	{
		delete mInternal;
	}
	FeatureBody& FeatureBody::instance() {
		static FeatureBody* body = nullptr;
		if (body == nullptr) {
			body = new FeatureBody("Body");
		}
		return *body;
	}
	void FeatureBody::addFeature(Feature* feature)
	{
		if (feature) {
			mInternal->featureList.push_back(feature);
		}
	}

	bool FeatureBody::removeFeature(Feature* feature)
	{
		if (feature) {
			int k = 0;
			for (int i = 0;i < mInternal->featureList.size();i++) {
				if (mInternal->featureList[i] != feature) {
					mInternal->featureList[k++] = mInternal->featureList[i];
				}
			}
			mInternal->featureList.resize(k);
			for (int i = 0;i < mInternal->featureList.size();i++) {
				if (mInternal->featureList[i]->getBaseFeature() == feature) {
					mInternal->featureList[i]->setBaseFeature(nullptr);
				}
			}
		}
		return false;
	}

	void FeatureBody::populateFeature(Feature* feature)
	{
		if (feature == nullptr) {
			return;
		}
		// makeDone() of a recomputed feature calls back into here, so the propagation is
		// recursive. A feature that ends up depending on something built from itself -
		// a sketch that projects an edge of the very pad it is the profile of, for
		// instance - makes that recursion cyclic, and the depth is what turns it into a
		// warning instead of a stack overflow.
		constexpr int kMaxPopulateDepth = 32;
		if (mInternal->populateDepth >= kMaxPopulateDepth) {
			CORE_WARN(
				"[FeatureBody] the feature graph looks cyclic around {0}; the recompute "
				"was stopped",
				feature->GetName());
			return;
		}
		++mInternal->populateDepth;

		std::vector<Feature*>stack;
		stack.push_back(feature);
		while (!stack.empty()) {
			Feature* curFeature = stack.back(); stack.pop_back();
			for (int i = 0; i < mInternal->featureList.size(); i++) {
				if (mInternal->featureList[i]->getBaseFeature()== curFeature) {
					mInternal->featureList[i]->execute();
					mInternal->featureList[i]->makeDone();
				}
				else {
					FeatureBaseProfile*  profile=dynamic_cast<FeatureBaseProfile*>(mInternal->featureList[i]);
					if (profile) {
						if (profile->getProfile() == curFeature) {
							mInternal->featureList[i]->execute();
							mInternal->featureList[i]->makeDone();
						}
					}
				}
			}
		}

		--mInternal->populateDepth;
	}

	Feature* FeatureBody::getLastBaseFeature(Feature* target)
	{
		if (mInternal->featureList.size() > 0) {
			for (int i = mInternal->featureList.size()-1;i>=0;i--) {
				if (mInternal->featureList[i] == target) {
					for (int k = i - 1; k >= 0; k--) {
						Feature3D* feature = dynamic_cast<Feature3D*>(mInternal->featureList[k]);
						if (feature) {
							return mInternal->featureList[k];
						}
					}
				}
			}
		}
		return nullptr;
	}
	bool FeatureBody::setBaseFeatureFor(Feature* feature)
	{
		if (feature) {
			Feature* baseFeature=getLastBaseFeature(feature);
			if (baseFeature) {
				feature->setBaseFeature(baseFeature);
				return true;
			}
		}
		return false;
	}
}
