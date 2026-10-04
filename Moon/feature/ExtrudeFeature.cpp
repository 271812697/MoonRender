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
#include "feature/SubShapeRef.h"
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
    namespace
    {
        /** True when the two faces sit in the same place.
         *
         * An operation carries the faces of the shape below into its result without
         * moving them, so a face of the result and that face of the shape below it
         * are the very same face whenever their centres of gravity agree. That is
         * what tells a face that can be re-anchored from one that only looks
         * similar. */
        bool IsSameFacePlacement(
            const Part::TopoShape& p_left,
            const Part::TopoShape& p_right)
        {
            Base::Vector3d left;
            Base::Vector3d right;
            if (!p_left.getCenterOfGravity(left) || !p_right.getCenterOfGravity(right)) {
                return false;
            }
            return (left - right).Length() <= Precision::Confusion();
        }
    }

    ExtrudeFeature::ExtrudeFeature(const std::string& p_name, int addSubType) :FeatureBaseProfile(p_name,addSubType==0? "Pad": "Pocket")
	{
        this->addSubType = addSubType;
	}
    ExtrudeFeature::~ExtrudeFeature()
	{
	}
	Part::TopoShape ExtrudeFeature::getToolShape()
	{
		return toolShape;
	}
	bool ExtrudeFeature::isToolSubtractive() const
	{
		return addSubType == 1;
	}
	void ExtrudeFeature::setUpToFaceReference(
		const Part::TopoShape& p_picked,
		Feature* p_feature,
		const std::string& p_reference)
	{
		upToFace = p_picked;
		upToFaceFeature = p_feature;
		upToFaceRef = p_reference;
		upToFaceNames.clear();
		if (upToFaceRef.empty()) {
			upToFaceFeature = nullptr;
			return;
		}
		// The names the picked face is known by: they are what a lookup in another
		// shape matches, which is what makes it possible to take the same face from
		// the shape below. A pick that comes from the preview carries no feature of
		// its own, and the preview is built the same way this feature is, so its own
		// shape is the next best place to read the names from.
		Feature* nameSource = p_feature != nullptr ? p_feature : this;
		CaptureSubShapeNames(
			nameSource->getWorldTopoShape(), upToFaceRef, upToFaceNames);
		CORE_INFO(
			"[UpToFace] {0}: '{1}' picked on '{2}'",
			GetName(),
			upToFaceRef,
			p_feature != nullptr ? p_feature->GetName() : "<preview>");

		// A face of the feature being built is not a face it can end at: that shape
		// holds the body *with* what this feature adds, while the prism is built
		// against the shape below it. Operations keep the faces of that shape, and
		// their names with them, so the picked face is looked for there and is only
		// kept when the two are really the same face.
		const bool pickedOnItself
			= p_feature == nullptr || p_feature == this;
		if (pickedOnItself && m_baseFeature != nullptr) {
			std::vector<std::string> names = upToFaceNames;
			Part::TopoShape onBase = ResolveSubShapeRef(
				*m_baseFeature, upToFaceRef, names, GetName());
			Part::TopoShape picked = upToFace;
			Feature::applyWorldTransform(*this, picked);
			if (!onBase.isNull() && IsSameFacePlacement(picked, onBase)) {
				upToFaceFeature = m_baseFeature;
				upToFaceNames = std::move(names);
				upToFace = onBase;
				CORE_INFO(
					"[UpToFace] {0}: '{1}' is a face of '{2}' as well; the reference is "
					"taken there",
					GetName(), upToFaceRef, m_baseFeature->GetName());
				return;
			}
		}
		else if (p_feature != nullptr && p_feature != this) {
			// Resolving here does two things at once: the preview has a face to
			// extrude to, and the mapped names of that face are captured, so the
			// reference can be re-resolved later and written to a document as a name
			// rather than as a position in an enumeration.
			Part::TopoShape resolved = ResolveSubShapeRef(
				*p_feature, upToFaceRef, upToFaceNames, GetName());
			if (!resolved.isNull()) {
				upToFace = resolved;
				return;
			}
		}

		// Nothing that can be written down: the face stays for this session only, and
		// a document says out loud that it cannot be carried.
		upToFaceFeature = nullptr;
		upToFaceRef.clear();
		upToFaceNames.clear();
		CORE_WARN(
			"{0}: the face picked for 'up to face' ({1}) cannot be tied to the shape "
			"this pad is built on - it is not the same face there; pick a face of the "
			"body below",
			GetName(),
			upToFaceRef);
	}
	bool ExtrudeFeature::execute()
	{
		Part::TopoShape face=getProfileFace();
		Part::TopoShape baseShape;
		if (m_baseFeature) {
			baseShape=getBaseTopoShape();
		}

		if (extrudeType == 2 && !upToFaceRef.empty()) {
			// The stored face is only a snapshot of the shape below: resolve the
			// reference again, so a recompute (or a document that was just read
			// back) extrudes to the face at the place it is in now. A reference
			// that was taken on this feature's own shape - which is what picking a
			// face of the body while this pad was already built comes down to - can
			// only mean the matching face of the shape below.
			Feature* source = upToFaceFeature == this ? m_baseFeature : upToFaceFeature;
			Part::TopoShape resolved;
			if (source != nullptr) {
				resolved = ResolveSubShapeRef(
					*source, upToFaceRef, upToFaceNames, GetName());
			}
			if (!resolved.isNull()) {
				upToFace = resolved;
				upToFaceFeature = source;
			}
			else {
				CORE_WARN(
					"{0}: the face it is padded up to ('{1}') could not be resolved "
					"any more; the face that was picked is used instead",
					GetName(), upToFaceRef);
			}
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
        if (extrudeType == 2 && upToFace.isNull()) {
            // The length is used instead, and that is said out loud: a pad that
            // cannot reach its face is worth a line in the log, but taking the
            // whole chain down with it is not - everything built on this feature
            // would come back without a shape.
            if (!upToFaceRef.empty()) {
                CORE_ERROR(
                    "{0}: pad up to face: the face '{1}' cannot be resolved any more, "
                    "the length is used instead",
                    GetName(), upToFaceRef);
            }
            else {
                CORE_WARN(
                    "{0}: pad up to face: no face was picked, the length is used "
                    "instead",
                    GetName());
            }
        }
        Part::TopoShape prism;
        if (extrudeType==2 && !upToFace.isNull()) {
            try
            {
                Part::TopoShape tempShape =face.makeElementFace(nullptr, "Part::FaceMakerBullseye");

                // The picked face can sit on either side of the sketch: the
                // direction only says which way the feature is built, not where
                // the face was picked. Take the sense from the geometry, the same
                // way FreeCAD flips the direction of an extrusion it cannot reach.
                gp_Dir prismDir(params.dir);
                Base::Vector3d profileCog;
                Base::Vector3d upToCog;
                if (face.getCenterOfGravity(profileCog)
                    && upToFace.getCenterOfGravity(upToCog)) {
                    const gp_Vec toFace(
                        upToCog.x - profileCog.x,
                        upToCog.y - profileCog.y,
                        upToCog.z - profileCog.z);
                    if (toFace.Dot(gp_Vec(prismDir)) < 0.0) {
                        prismDir.Reverse();
                    }
                }

                // A pad gives the shape below to the algorithm as its base, so the
                // profile lies on one of its faces - which is the configuration
                // BRepFeat_MakePrism is written for. A pocket cuts the prism out of
                // the base afterwards, so it keeps the profile as the base.
                Part::TopoShape prismBase;
                if (addSubType == 0) {
                    prismBase = baseShape;
                }

                prism = prism.makeElementPrismUntil(
                    prismBase,
                    tempShape,
                    supportShape,
                    upToFace, prismDir, Part::TopoShape::PrismMode::None,
                    true,
                    Part::OpCodes::Extrude);
                if (prism.isNull()) {
                    CORE_ERROR("Prim is Null");
                    return false;
                }
                // What this feature adds or takes away is the prism, whatever the
                // shape it is fused with below turns out to be.
                toolShape = prism;
                Part::TopoShape resShape;
                if (!baseShape.isNull()) {
                    if (addSubType == 0) {
                        // The base goes in first and the prism second, the way
                        // PartDesign does it (makeElementBoolean(Fuse, {base, prism})).
                        // The boolean is symmetric as a set operation, but the shape
                        // it hands back is not: built with the prism first, the fused
                        // body could no longer be merged by the *next* pad, and that
                        // one came out as two solids instead of one.
                        resShape = baseShape.makeElementFuse(prism);
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
            catch (const std::exception& e)
            {
                CORE_ERROR(
                    "{0}: up to face: {1}", GetName(), e.what());
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
                // The prism is this feature's own material, whatever it is fused
                // with or cut from below.
                toolShape = prism;
                Part::TopoShape resShape;
                if (!baseShape.isNull()) {
                    if (addSubType == 0) {
                        // Base first, prism second - see the note in the up-to-face
                        // branch above: the order decides whether the next pad can
                        // still merge with this result.
                        resShape = baseShape.makeElementFuse(prism);
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
