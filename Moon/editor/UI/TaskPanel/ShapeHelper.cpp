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
	};
	ShapeHelper::ShapeHelper(Feature* feature):mInternal(new Internal(this))
	{
		mInternal->selectObserver = SelectionManager::instance().AddObserver(SelectAny, this, &ShapeHelper::onSelectAny);
		mInternal->feature = feature;
	}
	ShapeHelper::~ShapeHelper()
	{
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
			//makeRefine for TopoShape
			Part::TopoShape& shape = feature->GetTopoShape();
			try
			{
				shape.setShape(shape.makeElementRefine());
			}
			catch (Standard_Failure& err)
			{
				CORE_ERROR("Refine generateShape failed:{}", err.GetMessageString());
			}
			//hide other features
			auto& view = GetService(Editor::Panels::SceneView);
			auto scene = view.GetScene();	
			for (auto& ac : scene->GetActors()) {
				if (dynamic_cast<Feature*>(ac)&&ac!=feature) {
					ac->SetActive(false);
					GetViewerWidget.modifyActorInTreeView(ac);
				}
			}
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
		auto& view = GetService(Editor::Panels::SceneView);
		auto scene = view.GetScene();
		auto preActor = scene->FindActorByName("TopoShapePreview");
		if (preActor) {
			GetViewerWidget.removeActorFromTreeView(preActor);
			scene->RemoveActor(preActor);
			delete preActor;
		}
	}
	void ShapeHelper::setFeature(Feature* feature)
	{
		mInternal->feature = feature;
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
		if (shapes.size() > 0) {
			if (shapes[1].getShape().ShapeType() == TopAbs_ShapeEnum::TopAbs_EDGE) {
				CORE_INFO("TopAbs_EDGE selected");
				onSelectEdge(shapes);
			}
			else if (shapes[1].getShape().ShapeType() == TopAbs_ShapeEnum::TopAbs_FACE)
			{
				CORE_INFO("TopAbs_FACE");
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
