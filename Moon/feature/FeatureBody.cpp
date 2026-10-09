
#include "feature/FeatureBody.h"
#include "SketcherFeature.h"
#include "feature/FeatureBaseProfile.h"
#include "feature/ExtrudeFeature.h"
#include "feature/LinearPatternFeature.h"
#include "feature/MirrorFeature.h"
#include "feature/PipeFeature.h"
#include "feature/PolarPatternFeature.h"
#include "core/log.h"
#include "TopoShape.h"
#include "Core/Global/ServiceLocator.h"
#include "editor/View/sceneview/viewerwidget.h"
#include "renderer/Context.h"
#include <Core/SceneSystem/Scene.h>
#include <algorithm>
#include <Standard_Failure.hxx>

namespace
{
	/** The features a feature is built from: the shape it is stacked on, the profile it
	 * is made of, a pipe's path, the face a pad ends at, and the instances a pattern
	 * repeats. Each of them has to be built before it, which is what a move has to keep
	 * true. */
	std::vector<MOON::Feature*> DependenciesOf(MOON::Feature* p_feature)
	{
		std::vector<MOON::Feature*> dependencies;
		if (p_feature == nullptr) {
			return dependencies;
		}
		const auto add = [&dependencies](MOON::Feature* p_dependency) {
			if (p_dependency != nullptr
				&& std::find(dependencies.begin(), dependencies.end(), p_dependency)
					== dependencies.end()) {
				dependencies.push_back(p_dependency);
			}
		};
		add(p_feature->getBaseFeature());
		if (auto* profile = dynamic_cast<MOON::FeatureBaseProfile*>(p_feature)) {
			add(profile->getProfile());
		}
		if (auto* pipe = dynamic_cast<MOON::PipeFeature*>(p_feature)) {
			add(pipe->spineFeature);
		}
		if (auto* extrude = dynamic_cast<MOON::ExtrudeFeature*>(p_feature)) {
			add(extrude->upToFaceFeature);
		}
		if (auto* pattern = dynamic_cast<MOON::PolarPatternFeature*>(p_feature)) {
			for (MOON::Feature* original : pattern->originals) {
				add(original);
			}
		}
		if (auto* pattern = dynamic_cast<MOON::LinearPatternFeature*>(p_feature)) {
			for (MOON::Feature* original : pattern->originals) {
				add(original);
			}
		}
		if (auto* mirror = dynamic_cast<MOON::MirrorFeature*>(p_feature)) {
			for (MOON::Feature* original : mirror->originals) {
				add(original);
			}
		}
		return dependencies;
	}
}

namespace MOON {
	namespace
	{
		/** The bodies of the current scene, and the one new features join. They are a
		 * list rather than a single object because a scene holds several bodies (see the
		 * header); the active one is what the panels work in. */
		std::vector<std::unique_ptr<FeatureBody>>& Bodies()
		{
			static std::vector<std::unique_ptr<FeatureBody>> bodies;
			return bodies;
		}
		FeatureBody*& ActiveBody()
		{
			static FeatureBody* active = nullptr;
			return active;
		}
	}

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
		, m_name(p_name)
	{
		// The anchor is the body's node in the scene: the tree shows the body by it, and
		// the features of the body hang under it. A body made before there is a scene
		// simply has none yet.
		if (auto* scene = GetService(Editor::Core::Context).sceneManager.GetCurrentScene()) {
			m_anchor = &scene->CreateActor(p_name, "Body");
		}
	}
	FeatureBody::~FeatureBody()
	{
		if (m_anchor != nullptr) {
			if (auto* scene = GetService(Editor::Core::Context).sceneManager.GetCurrentScene()) {
				scene->DelayDestroyActor({ m_anchor });
			}
			m_anchor = nullptr;
		}
		delete mInternal;
	}

	const std::vector<FeatureBody*>& FeatureBody::All()
	{
		static std::vector<FeatureBody*> bodies;
		bodies.clear();
		for (const std::unique_ptr<FeatureBody>& body : Bodies()) {
			bodies.push_back(body.get());
		}
		return bodies;
	}

	FeatureBody* FeatureBody::Create(const std::string& p_name)
	{
		Bodies().push_back(std::make_unique<FeatureBody>(p_name));
		FeatureBody* body = Bodies().back().get();
		SetActive(body);
		return body;
	}

	FeatureBody* FeatureBody::Active()
	{
		if (ActiveBody() == nullptr && !Bodies().empty()) {
			ActiveBody() = Bodies().back().get();
		}
		if (ActiveBody() == nullptr) {
			// A scene that has no body yet gets its first one here, the way the single
			// body used to be made on demand: whoever creates the first feature gets a
			// body to put it in.
			return Create("Body");
		}
		return ActiveBody();
	}

	void FeatureBody::SetActive(FeatureBody* p_body)
	{
		ActiveBody() = p_body;
	}

	void FeatureBody::SetName(const std::string& p_name)
	{
		m_name = p_name;
		// The anchor is how the body appears in the scene and in the tree, so it is
		// renamed along with the body.
		if (m_anchor != nullptr) {
			m_anchor->SetName(p_name);
		}
	}

	std::string FeatureBody::UniqueName(const std::string& p_base)
	{
		const auto taken = [](const std::string& p_name) {
			for (const std::unique_ptr<FeatureBody>& body : Bodies()) {
				if (body != nullptr && body->m_name == p_name) {
					return true;
				}
			}
			return false;
			};
		if (!taken(p_base)) {
			return p_base;
		}
		for (int index = 1; index < 1000; ++index) {
			char text[16] = {};
			snprintf(text, sizeof(text), "%s%03d", p_base.c_str(), index);
			if (!taken(text)) {
				return text;
			}
		}
		return p_base;
	}

	FeatureBody* FeatureBody::Of(const Core::ECS::Actor* p_anchor)
	{
		if (p_anchor == nullptr) {
			return nullptr;
		}
		for (const std::unique_ptr<FeatureBody>& body : Bodies()) {
			if (body->m_anchor == p_anchor) {
				return body.get();
			}
		}
		return nullptr;
	}

	std::vector<std::unique_ptr<FeatureBody>> FeatureBody::TakeAll()
	{
		ActiveBody() = nullptr;
		std::vector<std::unique_ptr<FeatureBody>> taken = std::move(Bodies());
		Bodies().clear();
		return taken;
	}

	void FeatureBody::Adopt(std::vector<std::unique_ptr<FeatureBody>> p_bodies)
	{
		Bodies() = std::move(p_bodies);
		ActiveBody() = Bodies().empty() ? nullptr : Bodies().back().get();
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
					rebuildFeature(mInternal->featureList[i]);
				}
				else {
					FeatureBaseProfile*  profile=dynamic_cast<FeatureBaseProfile*>(mInternal->featureList[i]);
					if (profile) {
						if (profile->getProfile() == curFeature) {
							rebuildFeature(mInternal->featureList[i]);
						}
					}
				}
			}
		}

		--mInternal->populateDepth;
	}

	void FeatureBody::rebuildFeature(Feature* p_feature)
	{
		if (p_feature == nullptr) {
			return;
		}
		// Nothing a feature throws may leave this function: it is reached from makeDone(),
		// which is reached from execute(), so there is no caller left to catch anything
		// and an escaping exception ends the application. Both families have to be named:
		// the kernel reports a null input as a Base::Exception (Part::NullShapeException,
		// say), which is not a Standard_Failure - and a feature that is asked to build
		// before its own input exists runs into one of them while a document is read.
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
	}

	bool FeatureBody::moveFeature(Feature* feature, int p_index)
	{
		if (feature == nullptr) {
			return false;
		}
		const int count = static_cast<int>(mInternal->featureList.size());
		if (count < 2) {
			return false;
		}
		p_index = std::clamp(p_index, 0, count - 1);
		int from = -1;
		for (int i = 0; i < count; ++i) {
			if (mInternal->featureList[i] == feature) {
				from = i;
				break;
			}
		}
		if (from < 0 || from == p_index) {
			return false;
		}

		// Which features are stacked on the chain, i.e. whose base is the 3D feature
		// before them: those are the ones a new order has to re-derive the base of. A
		// datum keeps the support it was picked on instead (a shape, not a position), so
		// it is not one of them.
		std::vector<Feature*> stackedOnTheChain;
		{
			Feature* previous3D = nullptr;
			for (Feature* candidate : mInternal->featureList) {
				if (dynamic_cast<Feature3D*>(candidate) != nullptr) {
					if (previous3D != nullptr && candidate->getBaseFeature() == previous3D) {
						stackedOnTheChain.push_back(candidate);
					}
					previous3D = candidate;
				}
			}
		}

		// The order a move would leave behind, and the check that nothing ends up above
		// what it is built from: a profile, a path or a shape that comes later cannot be
		// resolved at all, so flying blind here would build half a document.
		std::vector<Feature*> order = mInternal->featureList;
		order.erase(order.begin() + from);
		order.insert(order.begin() + p_index, feature);
		for (int i = 0; i < static_cast<int>(order.size()); ++i) {
			for (Feature* dependency : DependenciesOf(order[i])) {
				const auto at = std::find(order.begin(), order.end(), dependency);
				if (at != order.end() && std::distance(order.begin(), at) > i) {
					CORE_WARN(
						"[FeatureBody] {0} stays where it is: {1} is built from it, and "
						"moving it there would put it after {2}",
						feature->GetName(),
						order[i]->GetName(),
						dependency->GetName());
					return false;
				}
			}
		}

		mInternal->featureList = order;

		// The chain is the order, so the bases are re-derived from the new places: a
		// feature sits on the 3D feature before it, and one that ends up first in the
		// chain has nothing below it.
		for (Feature* candidate : stackedOnTheChain) {
			if (std::find(mInternal->featureList.begin(), mInternal->featureList.end(), candidate)
				== mInternal->featureList.end()) {
				continue;
			}
			Feature* previousBase = candidate->getBaseFeature();
			if (setBaseFeatureFor(candidate) == false) {
				candidate->setBaseFeature(nullptr);
			}
			if (candidate->getBaseFeature() != previousBase) {
				// The elements it referred to are named after the shape it sat on: those
				// names belong to the old base and would only be resorted to as a wrong
				// guess on the new one, so they are dropped and taken again.
				candidate->setReferenceNames({});
			}
		}

		// Everything from the new place of the moved feature on is rebuilt, in the order
		// it now has.
		for (int i = std::min(from, p_index); i < static_cast<int>(mInternal->featureList.size()); ++i) {
			rebuildFeature(mInternal->featureList[i]);
		}
		// The tree shows the chain in this order, so it is redrawn from it.
		GetService(::MOON::ViewerWidget).refreshTreeView();
		return true;
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
