#include "core/component/TopoShapeActor.h"

#include "editor/View/sceneview/viewerwidget.h"
#include "core/component/CTopoShape.h"
#include <Core/Global/ServiceLocator.h>
#include "Feature.h"
#include "feature/FeatureBody.h"
#include "feature/SubShapeRef.h"
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
	Part::TopoShape Feature::getWorldTopoShape()
	{
		Part::TopoShape shape = GetTopoShape();
		if (shape.isNull()) {
			return shape;
		}
		applyWorldTransform(*this, shape);
		return shape;
	}
	void Feature::applyWorldTransform(Feature& p_feature, Part::TopoShape& p_shape)
	{
		if (p_shape.isNull()) {
			return;
		}
		// Nothing to do while the feature sits where it was built, which is the common
		// case: only a pose the user gave it changes what a consumer has to see.
		const Maths::FMatrix4& world = p_feature.transform.GetWorldMatrix();
		bool identity = true;
		for (int i = 0; i < 16 && identity; ++i) {
			const float expected = (i % 5 == 0) ? 1.0f : 0.0f;
			identity = std::abs(world.data[i] - expected) < 1.0e-6f;
		}
		if (identity) {
			return;
		}

		const Base::Matrix4D matrix(
			world.data[0], world.data[1], world.data[2], world.data[3],
			world.data[4], world.data[5], world.data[6], world.data[7],
			world.data[8], world.data[9], world.data[10], world.data[11],
			world.data[12], world.data[13], world.data[14], world.data[15]
		);
		// A rigid motion only adds a location, which keeps the mapped names of the shape
		// alive - the references downstream (and the sketch that projects them) rely on
		// them. A scaled transform needs the topology rebuilt, which checkScale handles.
		CORE_INFO(
			"[Feature] {0}: handing its shape out with the transform of the actor",
			p_feature.GetName());
		p_shape.transformShape(matrix, /*copy*/false, /*checkScale*/true);
	}
	Part::TopoShape Feature::getBaseTopoShape()
	{
		if (m_baseFeature == nullptr) {
			CORE_WARN(
				"[Feature] {0}: has no base feature to take a shape from",
				GetName());
			return Part::TopoShape();
		}
		return m_baseFeature->getWorldTopoShape();
	}
	void Feature::setResultShape(Part::TopoShape p_shape)
	{
		if (!p_shape.isNull() && !p_shape.hasElementMap()) {
			CORE_WARN(
				"[Feature] {0}: it hands on a shape without mapped names; a feature "
				"built on it can only reference its elements by index, which a "
				"recompute is free to move",
				GetName());
		}
		topoShape->setShape(p_shape);
	}
	Part::TopoShape Feature::resolveBaseSubShape(int p_index)
	{
		if (m_baseFeature == nullptr || p_index < 0
			|| p_index >= static_cast<int>(subValues.size())) {
			return Part::TopoShape();
		}
		// Resolve against the base as a consumer sees it - its own transform included -
		// exactly like getBaseTopoShape() does. Otherwise a face or an edge picked for a
		// profile would be the one at the position the base was built at, not the
		// position it is shown at.
		Part::TopoShape baseShape = m_baseFeature->getWorldTopoShape();
		if (baseShape.isNull()) {
			return Part::TopoShape();
		}

		if (m_referenceNames.size() < subValues.size()) {
			m_referenceNames.resize(subValues.size());
		}

		// The lookup itself lives in one place, shared with the sketch's external
		// geometry: names first, then the names without their encoding levels, and
		// only then the index. It resolves against the *base* feature, because that
		// is the shape the reference was taken from - this feature's own shape is
		// what the reference produces, so looking there would hand an element of the
		// previous result back as if it came from the base.
		return ResolveSubShapeRef(
			*m_baseFeature, subValues[p_index], m_referenceNames[p_index], GetName());
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
