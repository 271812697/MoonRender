#include <numbers>
#include "core/component/TopoShapeActor.h"
#include "renderer/SceneView.h"
#include "TopoShapeOpCode.h"
#include "core/TopoNameDebug.h"
#include <cmath>
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
#include <gp_Trsf.hxx>
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
                    if (addSubType == 0) {
                        resShape = prism;
                    }
                    else {
                        // A pocket has nothing to cut from until it is given a base
                        // shape. Reporting that beats handing an empty shape to the
                        // features below, which would read as a shape that was lost.
                        CORE_ERROR(
                            "{0}: the pocket has no base shape to cut from", GetName());
                        return false;
                    }
                }
                setResultShape(resShape);
            
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
                // Without a taper angle the whole profile is one prism, the way
                // FreeCAD's pad does it: the sides come out as analytic surfaces
                // (planes and cylinders) instead of the B-splines a loft leaves
                // behind, and the holes of the profile are carried by the face
                // itself instead of being lofted one by one and cut away. Only a
                // taper angle needs the draft path, where every wire - the outer
                // one and the inner ones that become the holes - is lofted on its
                // own.
                const bool hasTaper
                    = std::fabs(params.taperAngleFwd) > Precision::Angular()
                    || std::fabs(params.taperAngleRev) > Precision::Angular();
                if (!hasTaper) {
                    Part::TopoShape profile = face;
                    if (params.solid && !profile.hasSubShape(TopAbs_FACE)) {
                        profile = profile.makeElementFace(
                            nullptr, params.faceMakerClass.c_str());
                    }
                    // A length behind the sketch is applied by moving the profile
                    // back and extruding the total length, which is what keeps a
                    // symmetric or a two sided pad centred on the sketch plane.
                    if (std::fabs(params.lengthRev) > Precision::Confusion()) {
                        gp_Trsf back;
                        back.SetTranslation(gp_Vec(params.dir) * (-params.lengthRev));
                        profile = profile.makeElementTransform(back);
                    }
                    prism = prism.makeElementPrism(
                        profile,
                        gp_Vec(params.dir) * (params.lengthFwd + params.lengthRev)
                    );
                }
                else {
                    std::vector<Part::TopoShape> drafts;
                    Part::ExtrusionHelper::makeElementDraft(
                        params,
                        face,
                        drafts, App::StringHasherRef()
                    );
                    if (drafts.empty()) {
                        return false;
                    }
                    // One draft is the prism itself (makeElementCompound hands a
                    // single shape back untouched), several drafts become one
                    // compound whose child maps repeat the names of the drafts.
                    // Either way the result is named after the drafts and gets no
                    // level of its own.
                    //
                    // That is the point of going through this one call: while the
                    // single wire case was tagged with mapSubElement(..., Extrude)
                    // and the several wire case passed the extrude code as the
                    // compound's postfix, the very same edge of the pad was named
                    // "g0;...;:U;MAK;XTR;:H:4,E;:H,E" for one wire and
                    // "g0;...;:U;MAK;XTR" for two, so a fillet that had recorded
                    // the first spelling stopped finding it, fell back to its index
                    // and filleted the solid the second wire produced.
                    prism.makeElementCompound(
                        drafts,
                        nullptr,
                        Part::TopoShape::SingleShapeCompoundCreationPolicy::returnShape
                    );
                }
                LogTopoElementNames(face, "profile");
                LogTopoElementNames(prism, "prism");
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
                    if (addSubType == 0) {
                        resShape = prism;
                    }
                    else {
                        // A pocket has nothing to cut from until it is given a base
                        // shape. Reporting that beats handing an empty shape to the
                        // features below, which would read as a shape that was lost.
                        CORE_ERROR(
                            "{0}: the pocket has no base shape to cut from", GetName());
                        return false;
                    }
                }
                setResultShape(resShape);
               
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
