#include "core/component/TopoShapeActor.h"

#include "editor/View/sceneview/viewerwidget.h"
#include "core/component/CTopoShape.h"
#include <Core/Global/ServiceLocator.h>
#include "Feature.h"
#include "feature/FeatureBody.h"
#include "SketcherFeature.h"
#include "Sketcher/SketcherObj.h"
#include "TopoShape.h"
#include "MappedName.h"
#include "IndexedName.h"
#include "core/log.h"
#include <algorithm>

namespace MOON {
	class Feature::Internal {
	public:
		Internal(Feature* f):self(f) {
		}
		~Internal() {
		}
	private:
		friend Feature;
		Feature* self = nullptr;
		Part::TopoShape previewShape;
	};
	Feature::Feature(const std::string& p_name,  const std::string& tag) :TopoActor( p_name, tag, true, false),mInternal(new Internal(this))
	{
		FeatureBody::instance().addFeature(this);
	}
	Feature::~Feature()
	{
		FeatureBody::instance().removeFeature(this);
		delete mInternal;
	}
	bool Feature::execute()
	{
		return true;
	}
	Part::TopoShape Feature::getBaseTopoShape()
	{
		return m_baseFeature->GetTopoShape();
	}
	Part::TopoShape Feature::resolveBaseSubShape(int p_index)
	{
		if (m_baseFeature == nullptr || p_index < 0
			|| p_index >= static_cast<int>(subValues.size())) {
			return Part::TopoShape();
		}
		auto comp = m_baseFeature->GetComponent<Core::ECS::Components::CTopoShape>();
		if (comp == nullptr) {
			return Part::TopoShape();
		}
		Part::TopoShape& baseShape = comp->GetTopoShape();

		if (m_referenceNames.size() < subValues.size()) {
			m_referenceNames.resize(subValues.size());
		}

		// 1) By the name recorded the first time this reference was used: this is
		// the path that survives a recompute of the base feature.
		const std::string& reference = subValues[p_index];
		for (const std::string& name : m_referenceNames[p_index]) {
			TopoDS_Shape byName = baseShape.getSubShape(name.c_str(), true);
			if (!byName.IsNull()) {
				CORE_INFO(
					"[TopoRef] {0}: '{1}' resolved by name '{2}'",
					GetName(), reference, name);
				return Part::TopoShape(byName);
			}
		}
		if (!m_referenceNames[p_index].empty()) {
			CORE_WARN(
				"[TopoRef] {0}: none of the {1} name(s) of reference '{2}' is in the "
				"base shape any more; falling back to its index",
				GetName(), m_referenceNames[p_index].size(), reference);
		}

		// 2) By index, the historical path. The reference is "<Type>_<index>" and
		// the index inside the shape is 1 based.
		const bool isFace = reference.rfind("Face", 0) == 0;
		const std::string typeName = isFace ? "Face" : "Edge";
		const TopAbs_ShapeEnum type = isFace ? TopAbs_FACE : TopAbs_EDGE;
		int index = 0;
		try {
			index = std::stoi(reference.substr(5));
		}
		catch (const std::exception&) {
			CORE_ERROR(
				"[TopoRef] {0}: cannot read an index out of the reference '{1}'",
				GetName(), reference);
			return Part::TopoShape();
		}
		Part::TopoShape result = baseShape.getSubTopoShape(type, index + 1);

		// Remember every name this index currently has, so the next resolve can use
		// them even if a recompute moves the sub-shape elsewhere. All of them are
		// kept because which one survives depends on how much history the shape
		// carries at that later time.
		const Data::IndexedName element
			= Data::IndexedName::fromConst(typeName.c_str(), index + 1);
		std::vector<std::string>& names = m_referenceNames[p_index];
		names.clear();
		for (const Data::MappedElement& candidate : baseShape.getElementMap()) {
			if (candidate.index == element) {
				names.push_back(candidate.name.toString());
			}
		}
		// Longest first: the most specific name is also the one that survives the
		// most operations downstream.
		std::sort(names.begin(), names.end(),
			[](const std::string& p_left, const std::string& p_right)
			{
				return p_left.size() > p_right.size();
			});

		if (!names.empty()) {
			std::string joined;
			for (const std::string& name : names) {
				joined += joined.empty() ? name : (" | " + name);
			}
			CORE_INFO(
				"[TopoRef] {0}: '{1}' resolved by index {2}, captured name(s) '{3}'",
				GetName(), reference, index, joined);
		}
		else {
			CORE_WARN(
				"[TopoRef] {0}: the base shape carries no mapped name for {1}_{2}; the "
				"reference stays index based and may break on recompute",
				GetName(), typeName, index);
		}
		return result;
	}
	Part::TopoShape Feature::getBaseTopoFaceShape()
	{
		return resolveBaseSubShape(0);
	}
	std::vector<Part::TopoShape> Feature::getBaseTopoFaceShapes()
	{
		std::vector<Part::TopoShape> ret;
		ret.reserve(subValues.size());
		for (int i = 0; i < static_cast<int>(subValues.size()); i++) {
			ret.push_back(resolveBaseSubShape(i));
		}
		return ret;
	}
	Part::TopoShape Feature::getBaseTopoEdgeShape()
	{
		return resolveBaseSubShape(0);
	}
	std::vector<Part::TopoShape> Feature::getBaseTopoEdgeShapes()
	{
		std::vector<Part::TopoShape> ret;
		ret.reserve(subValues.size());
		for (int i = 0; i < static_cast<int>(subValues.size()); i++) {
			ret.push_back(resolveBaseSubShape(i));
		}
		return ret;
	}
	Part::TopoShape& Feature::getPreviewShape()
	{
		return mInternal->previewShape;
	}
	void Feature::makeDone()
	{
		if (!hasInTree) {
			GetViewerWidget.addActorToTreeView(this);
			hasInTree = true;
		}
		auto comp =GetComponent<Core::ECS::Components::CTopoShape>();
		comp->discretizationShape();
		FeatureBody::instance().populateFeature(this);
	}
}
