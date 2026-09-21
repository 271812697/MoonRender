#include <numbers>
#include "core/component/TopoShapeActor.h"
#include "renderer/SceneView.h"
#include "TopoShapeOpCode.h"
#include "core/TopoNameDebug.h"
#include <Core/ResourceManagement/MaterialManager.h>
#include <Core/ECS/Components/CMaterialRenderer.h>
#include <Core/ECS/Components/CModelRenderer.h>
#include "Core/ECS/Components/CBatchMeshTriangle.h"
#include "Core/ECS/Components/CBatchMeshLine.h"
#include "Core/ResourceManagement/ModelManager.h"
#include "editor/View/sceneview/viewerwidget.h"
#include "core/component/CTopoShape.h"
#include <Core/Global/ServiceLocator.h>
#include <Core/SceneSystem/Scene.h>
#include "TopoShape.h"
#include "ExtrudeFeature.h"
#include "SketcherFeature.h"
#include "Sketcher/SketcherObj.h"
#include "core/log.h"
#include "App/ExtrusionHelper.h"
#include <gp_Pln.hxx>
#include <BRepTools.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <gp_Vec.hxx>
#include <gp_Dir.hxx>
#include <GeomAbs_Shape.hxx>
#include <ShapeFix_ShapeTolerance.hxx>
#include <BRepAlgo.hxx>
#include <ShapeAnalysis_Surface.hxx>
#include <BRepLProp_SLProps.hxx>
#include <Precision.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRep_Tool.hxx>
#include <TopoDS.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <BRepClass3d_SolidClassifier.hxx>

namespace MOON {
    ExtrudeFeature::ExtrudeFeature(const std::string& p_name, int addSubType) :FeatureBaseProfile(p_name,addSubType==0? "Pad": "Pocket")
	{
        this->addSubType = addSubType;
	}
    ExtrudeFeature::~ExtrudeFeature()
	{
	}
	bool ExtrudeFeature::execute()
	{
		Part::TopoShape face=getProfileFace();
        Part::TopoShape baseShape;
        if (m_baseFeature) {
            baseShape=getBaseTopoShape();
        }
       
		Part::ExtrusionParameters params;
		params.taperAngleFwd = angleForward * std::numbers::pi / 180.0;
		params.innerWireTaper = Part::InnerWireTaper::SameAsOuter;
		params.dir = finalDir;
		params.solid = true;
		params.lengthFwd = lengthForward;
		if (dirType == 0)      // 正向
		{

		}
		else if (dirType == 1) {
			params.lengthFwd *= -1;
			// ExtrusionHelper computes the taper offset as tan(angle) * length,
			// so negating the length would also negate the offset and turn an
			// inward taper into an outward one. Negating the angle as well keeps
			// the reversed prism the mirror image of the forward one.
			params.taperAngleFwd *= -1;
		}
		else if (dirType == 2) // 双向
		{
			params.lengthRev = lengthRev;
			params.taperAngleRev = angleRev * std::numbers::pi / 180.0;

		}
		else if (dirType == 3) // 对称
		{
			params.lengthRev = params.lengthFwd;
			// taperAngleFwd is already in radians (see above); converting it a
			// second time shrank the symmetric side's taper to ~0, so that side
			// came out as a straight extrusion.
			params.taperAngleRev = params.taperAngleFwd;
		}
        Part::TopoShape prism;
        if (extrudeType==2 && !upToFace.isNull()) {
            try
            {
                Part::TopoShape tempShape =face.makeElementFace(nullptr, "Part::FaceMakerBullseye");
                prism = prism.makeElementPrismUntil(
                    tempShape,
                    supportShape,
                    upToFace, -params.dir, Part::TopoShape::PrismMode::None,
                    true,
                    Part::OpCodes::Extrude);
                if (prism.isNull()) {
                    CORE_ERROR("Prim is Null");
                    return false;
                }
                Part::TopoShape resShape;
                if (!baseShape.isNull()) {
                    if (addSubType == 0) {
                        resShape = prism.makeElementFuse(baseShape);
                    }
                    else if (addSubType == 1) {
                        resShape = baseShape.makeElementCut(prism);
                    }
                }
                else {
                    if(addSubType == 0)
                    resShape = prism;
                }
                topoShape->setShape(resShape);
             
                LogTopoElementNames(resShape, "pad(up to face)");
                getPreviewShape() =resShape;
                return true;
            }
            catch (const std::exception&)
            {
                return false;
            }
        }
        else
        {
            try {
                std::vector<Part::TopoShape> drafts;
                Part::ExtrusionHelper::makeElementDraft(
                    params,
                    face,
                    drafts, App::StringHasherRef()
                );
                if (drafts.empty()) {
                    return false;
                }
                if (drafts.size() == 1) {
                    // makeElementCompound() hands a single shape back untouched, so it
                    // would carry no extrude tag while several drafts would get one.
                    // The same edge would then be named differently depending on how
                    // many curves the profile happens to have, and a reference taken
                    // before the profile grew a curve would stop resolving (it used to
                    // fall back to the index and fillet the wrong solid). Tagging the
                    // single draft here keeps one edge, one name.
                    prism = drafts.front();
                    LogTopoElementNames(face, "profile");
                    LogTopoElementNames(prism, "prism");
                    prism.mapSubElement(drafts, Part::OpCodes::Extrude);
                }
                else {
                    prism.makeElementCompound(
                        drafts,
                        Part::OpCodes::Extrude,
                        Part::TopoShape::SingleShapeCompoundCreationPolicy::returnShape
                    );
                }
                getPreviewShape() = prism;
                Part::TopoShape resShape;
                if (!baseShape.isNull()) {
                    if (addSubType == 0) {
                        resShape = prism.makeElementFuse(baseShape);
                    }
                    else if (addSubType == 1) {
                        resShape = baseShape.makeElementCut(prism);
                    }
                }
                else {
                    if (addSubType == 0)
                    resShape = prism;
                }
                topoShape->setShape(resShape);
               
                //LogTopoElementNames(resShape, "pad");
                return true;
            }
            catch (Base::ValueError e) {
                CORE_ERROR(e.getMessage());
                return false;
            }
            catch (...) {
                CORE_ERROR("Unknow Exception throw");
                return false;
            }
        }
        return false;
	}
}
