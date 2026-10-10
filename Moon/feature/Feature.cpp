#include "core/component/TopoShapeActor.h"

#include "editor/View/sceneview/viewerwidget.h"
#include "core/component/CTopoShape.h"
#include <Core/Global/ServiceLocator.h>
#include "Feature.h"
#include "feature/FeatureBody.h"
#include "feature/FeatureBaseProfile.h"
#include "feature/SubShapeRef.h"
#include "SketcherFeature.h"
#include "Sketcher/SketcherObj.h"
#include "Sketcher/SketcherObjManager.h"
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
		// A feature is made into the body that is active, and hangs under that body's
		// node in the scene: that is what lets a scene hold several bodies without every
		// panel having to be told which one it is editing.
		m_body = FeatureBody::Active();
		if (m_body != nullptr) {
			m_body->addFeature(this);
			if (Core::ECS::Actor* anchor = m_body->GetAnchor()) {
				SetParent(*anchor);
			}
		}
		else {
			CORE_WARN(
				"[Feature] {0} was made without a body, so it belongs to no chain", p_name);
		}
	}
	Feature::~Feature()
	{
		if (m_body != nullptr) {
			m_body->removeFeature(this);
			m_body = nullptr;
		}
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
		//
		// What is applied is the pose the feature has *inside its body* - its own
		// transform, the node of the body being the frame the chain lives in - and not
		// its full world matrix. The placement of the body belongs to the body as a
		// whole: the features hang under that node, so drawing, picking and the camera
		// follow it by themselves. A chain that baked it into the shapes it hands on
		// would see it twice - once in the shape every feature passes up, once again
		// when that shape is drawn through the actor matrices - and a feature that had
		// been rotated or moved as a whole would tear the chain apart. For a body left
		// at the origin (the common case) the two matrices are one and the same.
		const Maths::FMatrix4& local = p_feature.transform.GetLocalMatrix();
		bool identity = true;
		for (int i = 0; i < 16 && identity; ++i) {
			const float expected = (i % 5 == 0) ? 1.0f : 0.0f;
			identity = std::abs(local.data[i] - expected) < 1.0e-6f;
		}
		if (identity) {
			return;
		}

		const Base::Matrix4D matrix(
			local.data[0], local.data[1], local.data[2], local.data[3],
			local.data[4], local.data[5], local.data[6], local.data[7],
			local.data[8], local.data[9], local.data[10], local.data[11],
			local.data[12], local.data[13], local.data[14], local.data[15]
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
	void Feature::refineResultShape()
	{
		if (!isRefineActive() || topoShape->isNull()) {
			return;
		}
		// The refinement belongs to the moment the result is committed rather than to
		// execute(): a task panel drags its parameters through execute() on every mouse
		// move and only displays what comes out, so refining there would merge the
		// whole shape again and again while the panels are being dragged - and the
		// shape that is displayed is the raw one anyway. Merging is idempotent, so a
		// shape that is already refined comes out unchanged.
		try {
			topoShape->setShape(topoShape->makeElementRefine());
		}
		catch (const Standard_Failure& err) {
			CORE_ERROR(
				"[Feature] {0}: the shape could not be refined ({1}); it is used as "
				"it is",
				GetName(),
				err.GetMessageString());
		}
	}
	SketcherObj* Feature::findBaseSketch()
	{
		for (Feature* f = getBaseFeature(); f != nullptr; f = f->getBaseFeature()) {
			auto* profile = dynamic_cast<FeatureBaseProfile*>(f);
			if (profile == nullptr || profile->getProfile() == nullptr) {
				continue;
			}
			return profile->getProfile()->getSketcherObj();
		}
		// Nothing in the chain was built from a sketch, so the one being edited is
		// the next best guess.
		return SketcherObjManager::instance().GetCurrentActiveSketcherObj();
	}
	Part::TopoShape Feature::getToolShape()
	{
		// The features that build something of their own override this; everything
		// else - sketches, datums, the transform features - has no material to
		// transform on its own.
		return Part::TopoShape();
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
		// This is the commit point: OK of a task panel, the end of a recompute and
		// reading a document all come through here, and each of them has to leave the
		// shape the same way - built up to a face, with the coplanar faces it fused
		// merged into one.
		refineResultShape();
		auto comp =GetComponent<Core::ECS::Components::CTopoShape>();
		comp->discretizationShape();
		if (m_body != nullptr) {
			m_body->populateFeature(this);
		}
	}
	Part::TopoShape Feature3D::getToolShape() {
		return GetTopoShape();
	}
}
