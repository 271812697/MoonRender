
#include "feature/FeatureBody.h"
#include "SketcherFeature.h"
#include "feature/FeatureBaseProfile.h"
#include "core/log.h"
#include "TopoShape.h"
#include <Standard_Failure.hxx>

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

		// One feature of the propagation: it is rebuilt and committed, and nothing it
		// throws may leave this function.
		//
		// This is reached from makeDone(), which is reached from execute(), so there is
		// no caller left to catch anything and an escaping exception ends the
		// application. Both families have to be named: the kernel reports a null input
		// as a Base::Exception (Part::NullShapeException, say), which is not a
		// Standard_Failure - and a feature that is asked to build before its own input
		// exists runs into one of them while a document is being read.
		const auto rebuild = [](Feature* p_feature) {
			try {
				if (!p_feature->execute()) {
					CORE_ERROR("[FeatureBody] {0} could not be built", p_feature->GetName());
				}
			}
			catch (const Base::Exception& e) {
				CORE_ERROR("[FeatureBody] {0}: {1}", p_feature->GetName(), e.what());
			}
			catch (Standard_Failure& e) {
				CORE_ERROR("[FeatureBody] {0}: {1}", p_feature->GetName(), e.GetMessageString());
			}
			try {
				p_feature->makeDone();
			}
			catch (const Base::Exception& e) {
				CORE_ERROR("[FeatureBody] {0}: {1}", p_feature->GetName(), e.what());
			}
			catch (Standard_Failure& e) {
				CORE_ERROR("[FeatureBody] {0}: {1}", p_feature->GetName(), e.GetMessageString());
			}
		};

		std::vector<Feature*>stack;
		stack.push_back(feature);
		while (!stack.empty()) {
			Feature* curFeature = stack.back(); stack.pop_back();
			for (int i = 0; i < mInternal->featureList.size(); i++) {
				if (mInternal->featureList[i]->getBaseFeature()== curFeature) {
					rebuild(mInternal->featureList[i]);
				}
				else {
					FeatureBaseProfile*  profile=dynamic_cast<FeatureBaseProfile*>(mInternal->featureList[i]);
					if (profile) {
						if (profile->getProfile() == curFeature) {
							rebuild(mInternal->featureList[i]);
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
	const std::vector<Feature*>& FeatureBody::getFeatures() const
	{
		return mInternal->featureList;
	}
	void FeatureBody::setFeatures(const std::vector<Feature*>& p_features)
	{
		mInternal->featureList = p_features;
	}
	void FeatureBody::clear()
	{
		// The actors go with the features: RemoveFromScene takes a feature's actor - and,
		// through the scene, the render anchors and the topology actors hanging under it -
		// out of the scene. The features themselves are deleted afterwards, from a copy,
		// because a destructor unlists itself from the very list being walked.
		const std::vector<Feature*> features = mInternal->featureList;
		for (Feature* feature : features) {
			if (feature != nullptr) {
				feature->RemoveFromScene();
			}
		}
		for (Feature* feature : features) {
			delete feature;
		}
		mInternal->featureList.clear();
	}
}
