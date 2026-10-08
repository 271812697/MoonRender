#include "editor/UI/TaskPanel/ShapeHelper.h"
#include "editor/View/sceneview/viewerwidget.h"
#include "TopoShape.h"
#include "core/component/TopoShapeActor.h"
#include "core/component/CTopoShape.h"
#include "Core/Global/ServiceLocator.h"
#include "core/ViewTool.h"
#include "core/log.h"
#include "renderer/SceneView.h"
#include "core/SelectionManager.h"
#include "Interactive/Interactive/ExecuteCommand.h"
#include "Interactive/Interactive/EventObject.h"
#include "feature/Feature.h"
#include "feature/FeatureBody.h"
#include <algorithm>
#include <Core/ECS/Components/CMaterialRenderer.h>
#include <tracy/Tracy.hpp>
#include <GProp_GProps.hxx>
#include <BRepGProp.hxx>
#include <gp_Lin.hxx>
#include <gce_MakeLin.hxx>
#include <BRepIntCurveSurface_Inter.hxx>
#include <gce_MakeDir.hxx>
#include <TopoDS.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
namespace MOON {
	struct cutTopoShapeFaces
	{
		Part::TopoShape face;
		double distsq;
	};
	std::vector<cutTopoShapeFaces> findAllFacesCutBy(
		const Part::TopoShape& shape,
		const Part::TopoShape& face,
		const gp_Dir& dir
	)
	{
		// Find the centre of gravity of the face
		GProp_GProps props;
		BRepGProp::SurfaceProperties(face.getShape(), props);
		gp_Pnt cog = props.CentreOfMass();

		// create a line through the centre of gravity
		gp_Lin line = gce_MakeLin(cog, dir);

		// Find intersection of line with all faces of the shape
		std::vector<cutTopoShapeFaces> result;
		BRepIntCurveSurface_Inter mkSection;
		// TODO: Less precision than Confusion() should be OK?

		for (mkSection.Init(shape.getShape(), line, Precision::Confusion()); mkSection.More();
			mkSection.Next()) {
			gp_Pnt iPnt = mkSection.Pnt();
			double dsq = cog.SquareDistance(iPnt);

			if (dsq < Precision::Confusion()) {
				continue;  // intersection with original face
			}

			// Find out which side of the original face the intersection is on
			gce_MakeDir mkDir(cog, iPnt);
			if (!mkDir.IsDone()) {
				continue;  // some error (appears highly unlikely to happen, though...)
			}

			if (mkDir.Value().IsOpposite(dir, Precision::Confusion())) {
				continue;  // wrong side of face (opposite to extrusion direction)
			}

			cutTopoShapeFaces newF;
			newF.face = mkSection.Face();
			newF.face.mapSubElement(shape);
			newF.distsq = dsq;
			result.push_back(newF);
		}

		return result;
	}
	class ShapeHelper::Internal
	{
	public:
		Internal(ShapeHelper* s):self(s) {
		}
		~Internal() {
			SelectionManager::instance().RemoveObserver(selectObserver.tag);
			delete selectObserver.command;
		}
	protected:
		Feature* feature = nullptr;
	private:
		friend ShapeHelper;
		ShapeHelper* self = nullptr;
		
		
		// 预览用的Actor
		Part::TopoShape m_previewShape;
		Part::TopoShape m_generateShape;
		TopoActor* m_previewActor = nullptr;
		std::string name="GenerateShape";
		ExecuteCommandPair selectObserver;
		/** While a feature is edited its own shape is taken out of the scene and the
		 * shape below is shown instead; both flags remember what was on so that
		 * closing or cancelling the panel puts it back. */
		bool rolledBack = false;
		bool featureWasActive = false;
		bool baseWasActive = false;
	};
	ShapeHelper::ShapeHelper(Feature* feature):mInternal(new Internal(this))
	{
		mInternal->selectObserver = SelectionManager::instance().AddObserver(SelectAny, this, &ShapeHelper::onSelectAny);
		setFeature(feature);
	}
	ShapeHelper::~ShapeHelper()
	{
		// A panel can also be taken down without its cancel - the task view replaces
		// it, say - and the feature it rolled back has to go back on screen then too.
		// The feature is only touched while it is still part of the body: a panel that
		// was cancelled deletes the feature it created, and that pointer is dangling
		// by now.
		Feature* feature = mInternal->feature;
		if (mInternal->rolledBack && feature != nullptr) {
			const std::vector<Feature*>& features
				= FeatureBody::instance().getFeatures();
			if (std::find(features.begin(), features.end(), feature) != features.end()) {
				restoreFeature();
			}
		}
		delete mInternal;
	}
	void ShapeHelper::previewShape()
	{
		ZoneScoped;
		auto feature=getFeature();
		if (feature) {
			if (feature->execute()) {
				mInternal->m_previewShape = feature->getPreviewShape();
				if (mInternal->m_previewActor == nullptr) {
					auto& view = GetService(Editor::Panels::SceneView);
					auto scene = view.GetScene();
					auto preActor = scene->FindActorByName("TopoShapePreview");
					if (preActor) {
						scene->RemoveActor(preActor);
					}
					mInternal->m_previewActor = new TopoActor("TopoShapePreview", "TopoShape", true);
				}
				const auto& topoComp = mInternal->m_previewActor->GetComponent<Core::ECS::Components::CTopoShape>();
				Part::TopoShape& topo = topoComp->GetTopoShape();
				topo.setShape(mInternal->m_previewShape);
				topoComp->discretizationShape();
				auto MatRender = mInternal->m_previewActor->GetChild("AllFaces")->GetComponent<Core::ECS::Components::CMaterialRenderer>();
				Core::Resources::Material* tempMat = MatRender->GetMaterialAtIndex(0);
				tempMat->SetProperty("u_Albedo", Maths::FVector4(mPreviewOption.r, mPreviewOption.g, mPreviewOption.b, mPreviewOption.a));
				if (mPreviewOption.isTransparent) {
					// Both styles are spelled out completely: a panel can switch
					// between them (a pattern that takes material away is drawn
					// differently from one that adds it), and a flag left over from
					// the other style would keep the preview hidden or on top.
					tempMat->SetTransparent(true);
					tempMat->SetBlendable(true);
					tempMat->SetDepthWriting(true);
					tempMat->SetDepthTest(true);
					tempMat->SetDrawOrder(0);
				}
				else {
					tempMat->SetTransparent(false);
					if (mPreviewOption.isBlend) {
						tempMat->SetBlendable(true);
						tempMat->SetDepthTest(false);
						tempMat->SetDepthWriting(false);
						tempMat->SetDrawOrder(10000);
					}
				}
				if (!mPreviewOption.useDomainColor) {
					tempMat->AddFeature("DISABLE_DOMAIN_COLOR");
				}
				tempMat->SetBackfaceCulling(false);
				tempMat->SetFrontfaceCulling(false);
			}
			else
			{
				CORE_ERROR("Generate Shape failed");
			}
		}
	}
	void ShapeHelper::generateFinalShape()
	{
		ZoneScoped;
		auto feature = getFeature();
		if (feature) {
			// The refinement of the result is not done here any more: it belongs to
			// Feature::makeDone(), the point every committed result goes through, so
			// that a document that is read back produces the same shape this panel
			// does. makeDone() below is where it happens.
			//hide other features
			auto& view = GetService(Editor::Panels::SceneView);
			auto scene = view.GetScene();	
			for (auto& ac : scene->GetActors()) {
				if (dynamic_cast<Feature*>(ac)&&ac!=feature) {
					ac->SetActive(false);
					GetViewerWidget.modifyActorInTreeView(ac);
				}
			}
			// The feature this panel belongs to is the tip again: it was taken out of
			// the scene while the panel was open (see rollBackToBase).
			if (!feature->IsActive()) {
				feature->SetActive(true);
				GetViewerWidget.modifyActorInTreeView(feature);
			}
			mInternal->rolledBack = false;
			//add to treeview if not exist and discterize shape
			feature->makeDone();
			//clear preview
			auto preActor = scene->FindActorByName("TopoShapePreview");
			if (preActor) {
				GetViewerWidget.removeActorFromTreeView(preActor);
				scene->RemoveActor(preActor);
				delete preActor;
			}
		}
	}
	void ShapeHelper::clearPreviewShape()
	{
		restoreFeature();
		// A cancelled panel leaves the feature with the shape its last preview
		// computed, and that one is the raw result. Refining it here keeps it like
		// every committed one instead of leaving a seam until the next recompute.
		Feature* feature = getFeature();
		if (feature != nullptr) {
			const std::vector<Feature*>& features
				= FeatureBody::instance().getFeatures();
			if (std::find(features.begin(), features.end(), feature) != features.end()) {
				feature->refineResultShape();
				// Discretizing again is what puts the merged faces on screen: the
				// topology tree that is drawn was built from the raw shape.
				feature->GetComponent<Core::ECS::Components::CTopoShape>()
					->discretizationShape();
			}
		}
		auto& view = GetService(Editor::Panels::SceneView);
		auto scene = view.GetScene();
		auto preActor = scene->FindActorByName("TopoShapePreview");
		if (preActor) {
			GetViewerWidget.removeActorFromTreeView(preActor);
			scene->RemoveActor(preActor);
			delete preActor;
		}
	}
	void ShapeHelper::rollBackToBase()
	{
		Feature* feature = getFeature();
		if (feature == nullptr || mInternal->rolledBack) {
			return;
		}
		// A feature that was never built has nothing in the scene, so there is nothing
		// to take out of it - and everything below it is still on screen, which is
		// exactly what the picks have to land on.
		if (feature->GetTopoShape().isNull()) {
			return;
		}
		mInternal->featureWasActive = feature->IsActive();
		if (mInternal->featureWasActive) {
			feature->SetActive(false);
			GetViewerWidget.modifyActorInTreeView(feature);
		}
		Feature* base = feature->getBaseFeature();
		mInternal->baseWasActive = base != nullptr && base->IsActive();
		if (base != nullptr && !mInternal->baseWasActive) {
			base->SetActive(true);
			GetViewerWidget.modifyActorInTreeView(base);
		}
		mInternal->rolledBack = true;
		CORE_INFO(
			"[ShapeHelper] {0}: the tip is rolled back to '{1}' while its panel is open",
			feature->GetName(),
			base != nullptr ? base->GetName() : "<nothing>");
	}
	void ShapeHelper::restoreFeature()
	{
		if (!mInternal->rolledBack) {
			return;
		}
		mInternal->rolledBack = false;
		Feature* feature = getFeature();
		if (feature == nullptr) {
			return;
		}
		Feature* base = feature->getBaseFeature();
		if (base != nullptr && !mInternal->baseWasActive) {
			base->SetActive(false);
			GetViewerWidget.modifyActorInTreeView(base);
		}
		if (mInternal->featureWasActive && !feature->IsActive()) {
			feature->SetActive(true);
			GetViewerWidget.modifyActorInTreeView(feature);
		}
	}
	void ShapeHelper::setFeature(Feature* feature)
	{
		mInternal->feature = feature;
		rollBackToBase();
	}
	Feature* ShapeHelper::getFeature()
	{
		return mInternal->feature ;
	}
	void ShapeHelper::setFeatureSubValues(const std::vector<std::string>& subValues)
	{
		mInternal->feature->setSubValues(subValues);
		//mInternal->feature->setsubValues;
	}
	Part::TopoShape& ShapeHelper::getPreviewShape()
	{
		return mInternal->m_previewShape;
	}

	void ShapeHelper::onSelectAny()
	{
		ZoneScoped;
		std::vector<Part::TopoShape>shapes;
		ViewTool::getSelectedTopoShape(shapes);
		// getSelectedTopoShape() pushes the whole shape first and the picked
		// sub-shape second, so both entries are needed here.
		if (shapes.size() >= 2) {
			if (shapes[1].getShape().ShapeType() == TopAbs_ShapeEnum::TopAbs_EDGE) {
				CORE_INFO("TopAbs_EDGE selected");
				onSelectEdge(shapes);
			}
			else if (shapes[1].getShape().ShapeType() == TopAbs_ShapeEnum::TopAbs_FACE)
			{
				CORE_INFO("TopAbs_FACE selected");
				onSelectFace(shapes);
			}
		}
	}
	void ShapeHelper::onSelectEdge(const std::vector<Part::TopoShape>& edge)
	{
	}
	void ShapeHelper::onSelectFace(const std::vector<Part::TopoShape>& face)
	{
	}
	void ShapeHelper::setGenerateShapeName(const char* name)
	{
		mInternal->name = name;
	}
	void ShapeHelper::getUpToFace(Part::TopoShape& upToFace, const Part::TopoShape& support, const Part::TopoShape& sketchshape, const std::string& method, gp_Dir& dir)
	{
		if ((method == "UpToLast") || (method == "UpToFirst")) {
			std::vector<cutTopoShapeFaces> cfaces
				= findAllFacesCutBy(support, sketchshape, dir);
			if (cfaces.empty()) {
				throw Base::ValueError("SketchBased: No faces found in this direction");
			}

			// Find nearest/furthest face
			std::vector<cutTopoShapeFaces>::const_iterator it, it_near, it_far;
			it_near = it_far = cfaces.begin();
			for (it = cfaces.begin(); it != cfaces.end(); it++) {
				if (it->distsq > it_far->distsq) {
					it_far = it;
				}
				else if (it->distsq < it_near->distsq) {
					it_near = it;
				}
			}
			upToFace = (method == "UpToLast" ? it_far->face : it_near->face);
		}
		else if (findAllFacesCutBy(upToFace, sketchshape, dir).empty()) {
			dir = -dir;
		}

		if (upToFace.shapeType(true) != TopAbs_FACE) {
			if (!upToFace.hasSubShape(TopAbs_FACE)) {
				throw Base::ValueError("SketchBased: Up to face: No face found");
			}
			upToFace = upToFace.getSubTopoShape(TopAbs_FACE, 1);
		}

		TopoDS_Face face = TopoDS::Face(upToFace.getShape());

		// Check that the upToFace does not intersect the sketch face and
		// is not parallel to the extrusion direction
		BRepAdaptor_Surface adapt(face);

		if (adapt.GetType() == GeomAbs_Plane) {
			if (dir.IsNormal(adapt.Plane().Axis().Direction(), Precision::Confusion())) {
				throw Base::ValueError(
					"SketchBased: Up to face: Must not be parallel to extrusion direction!"
				);
			}
		}

		// We must measure from sketchshape, not supportface, here
		BRepExtrema_DistShapeShape distSS(sketchshape.getShape(), face);
		if (distSS.Value() < Precision::Confusion()) {
			throw Base::ValueError("SketchBased: Up to face: Must not intersect sketch!");
		}
	}
}
