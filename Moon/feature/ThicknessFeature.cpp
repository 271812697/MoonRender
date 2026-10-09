#include "core/component/TopoShapeActor.h"
#include "renderer/SceneView.h"
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
#include "ThicknessFeature.h"
#include "core/log.h"
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
#include <BRepCheck_Analyzer.hxx>
#include <map>
#include <vector>

namespace MOON {
	ThicknessFeature::ThicknessFeature(const std::string& p_name) :Feature3D(p_name, "Thickness")
	{
	}
	ThicknessFeature::~ThicknessFeature()
	{
	}
	bool ThicknessFeature::execute()
	{
        const double tol = Precision::Confusion();
        const double thickness = (reverse ? -1. : 1.) * thickNessValue;
        int join = joinType;
        // We do not offer the tangent join type; the kernel has no such join for a
        // thickness. FreeCAD drops it the same way.
        if (join == 1) {
            join = 2;
        }
        // Only the skin mode is implemented by the offset builder: BRepOffset_Pipe and
        // BRepOffset_RectoVerso are documented as not implemented, and handing them over
        // does not fail with an exception - it takes the process with it (a mode picked
        // in the panel did exactly that). The request is built as a skin and said out
        // loud instead of being obeyed.
        short offsetMode = static_cast<short>(mode);
        if (offsetMode != 0) {
            CORE_WARN(
                "[Thickness] {0}: offset mode {1} is not implemented by the kernel; the "
                "skin mode is used instead",
                GetName(),
                offsetMode);
            offsetMode = 0;
        }
        if (fabs(thickness) <= 2 * tol) {
            return false;
        }

        // The shape below, and the faces this thickness opens. Both throws are caught: a
        // recompute can reach this feature while the shape below is still being built,
        // and an exception that leaves execute() ends the application.
        Part::TopoShape topShape;
        try
        {
            topShape = getBaseTopoShape();
        }
        catch (Base::Exception& e)
        {
            CORE_ERROR("[Thickness] {0}: {1}", GetName(), e.what());
            return false;
        }
        catch (Standard_Failure& e)
        {
            CORE_ERROR("[Thickness] {0}: {1}", GetName(), e.GetMessageString());
            return false;
        }
        if (topShape.isNull()) {
            // Nothing to thicken yet: the chain is recomputed by walking the links, and
            // this feature is reached from its base before the shape of that base exists.
            // The pass that runs once everything is built does the work.
            CORE_WARN(
                "[Thickness] {0}: the shape it thickens is not there yet; this pass is "
                "skipped",
                GetName());
            return false;
        }

        // Which solid each face that is opened belongs to.
        //
        // This is FreeCAD's PartDesign::Thickness, and the grouping per solid is what
        // makes a thickness work at all: the shape below a dress-up is normally the
        // compound a boolean produced, while BRepOffsetAPI_MakeThickSolid only builds a
        // *solid*. Handed the compound it fails with "BRep_API: command not done", which
        // says nothing about the real reason - a thickness is computed one solid at a
        // time, and each solid is given only the faces that belong to it.
        std::map<int, std::vector<Part::TopoShape>> closeFaces;
        for (int i = 0; i < static_cast<int>(subValues.size()); ++i) {
            Part::TopoShape face;
            try
            {
                face = resolveBaseSubShape(i);
            }
            catch (Base::Exception& e)
            {
                CORE_ERROR("[Thickness] {0}: {1}", GetName(), e.what());
                return false;
            }
            catch (Standard_Failure& e)
            {
                CORE_ERROR("[Thickness] {0}: {1}", GetName(), e.GetMessageString());
                return false;
            }
            if (face.isNull()) {
                CORE_ERROR(
                    "[Thickness] {0}: the face '{1}' it opens is not in the shape below "
                    "any more",
                    GetName(),
                    subValues[i]);
                return false;
            }
            const int solidIndex = topShape.findAncestor(face.getShape(), TopAbs_SOLID);
            if (solidIndex == 0) {
                CORE_WARN(
                    "[Thickness] {0}: '{1}' is not a face of a solid of the shape below; "
                    "it is ignored",
                    GetName(),
                    subValues[i]);
                continue;
            }
            closeFaces[solidIndex].push_back(face);
        }

        const int solidCount = static_cast<int>(topShape.countSubShapes(TopAbs_SOLID));
        if (solidCount == 0) {
            CORE_ERROR("[Thickness] {0}: the shape below has no solid to hollow out", GetName());
            return false;
        }

        // One hollow per solid, and the result keeps every solid of the body: one that
        // has no face to open goes into it unchanged.
        std::vector<Part::TopoShape> hollowed;
        auto next = closeFaces.begin();
        for (int index = 1; index <= solidCount; ++index) {
            Part::TopoShape solid = topShape.getSubTopoShape(TopAbs_SOLID, index);
            if (solid.isNull()) {
                continue;
            }
            // FreeCAD hands the kernel a solid whose shells face outwards - its
            // TopoShape::getSolid() fixes the orientation of every result a feature
            // stores - while a boolean here keeps whatever orientation the kernel left
            // it with. A shell that faces inwards makes the offset unusable, and the
            // kernel then refuses with "command not done" whatever the thickness is.
            solid.fixSolidOrientation();
            if (next == closeFaces.end() || index < next->first) {
                hollowed.push_back(solid);
                continue;
            }
            const std::vector<Part::TopoShape>& faces = next->second;
            try
            {
                hollowed.push_back(solid.makeElementThickSolid(
                    faces,
                    thickness,
                    tol,
                    intersection,
                    false,
                        offsetMode,
                    static_cast<Part::JoinType>(join)));
            }
            catch (Standard_Failure& e)
            {
                // The kernel's message says nothing about the input, and a shape that is
                // not a valid solid is the usual reason for a flat refusal, so that is
                // what is reported with the request - the check is cheap and only runs
                // once something has already failed.
                const bool valid
                    = BRepCheck_Analyzer(solid.getShape()).IsValid() != Standard_False;
                // The two reasons a valid solid can be refused need different answers:
                // the thickness is larger than a wall of the shape (lower it) or the
                // solid cannot be offset at all - a sweep that twists or self
                // intersects, say - and then no value will do. One probe of a tenth of
                // a millimetre against the same faces tells them apart.
                bool smallOffsetWorks = false;
                try
                {
                    solid.makeElementThickSolid(
                        faces,
                        (thickness < 0.0 ? -1.0 : 1.0) * 0.1,
                        tol,
                        intersection,
                        false,
                        static_cast<short>(mode),
                        static_cast<Part::JoinType>(join));
                    smallOffsetWorks = true;
                }
                catch (Standard_Failure&)
                {
                    smallOffsetWorks = false;
                }
                catch (Base::Exception&)
                {
                    smallOffsetWorks = false;
                }
                // Is the shape below the dress-up the one that cannot be offset, or is it
                // the feature built on top of it (the pipe fused into the body)? The shape
                // the base feature itself was built on answers that without touching the
                // model - only the answer is reported.
                std::string cause = "the shape could not be traced further back";
                Feature* belowFeature = m_baseFeature != nullptr
                    ? m_baseFeature->getBaseFeature() : nullptr;
                if (belowFeature != nullptr)
                {
                    Part::TopoShape below = belowFeature->getWorldTopoShape();
                    if (below.countSubShapes(TopAbs_SOLID) > 0
                        && below.countSubShapes(TopAbs_FACE) > 0)
                    {
                        try
                        {
                            Part::TopoShape probeSolid
                                = below.getSubTopoShape(TopAbs_SOLID, 1);
                            probeSolid.fixSolidOrientation();
                            std::vector<Part::TopoShape> probeFaces{
                                below.getSubTopoShape(TopAbs_FACE, 1) };
                            probeSolid.makeElementThickSolid(
                                probeFaces,
                                thickness,
                                tol,
                                intersection,
                                false,
                                offsetMode,
                                static_cast<Part::JoinType>(join));
                            cause
                                = "the body without the feature above it can be hollowed, "
                                  "so the feature fused onto it is what defeats the offset";
                        }
                        catch (Standard_Failure&)
                        {
                            cause
                                = "the body without the feature above it cannot be hollowed "
                                  "either, so the offset fails on the body itself";
                        }
                        catch (Base::Exception&)
                        {
                            cause
                                = "the body without the feature above it cannot be hollowed "
                                  "either, so the offset fails on the body itself";
                        }
                    }
                }
                CORE_ERROR(
                    "[Thickness] {0}: a thickness of {1} mm ({2} mode, {3} join) could not "
                    "be built on solid {4} of {5}, opening {6} face(s); the solid is {7}: "
                    "{8} - {9} ({10})",
                    GetName(),
                    thickness,
                    mode,
                    join,
                    index,
                    solidCount,
                    faces.size(),
                    valid ? "valid" : "NOT valid",
                    e.GetMessageString(),
                    smallOffsetWorks
                    ? "0.1 mm of the same shape succeeds, so the thickness is larger than "
                      "a wall of it"
                    : "0.1 mm fails as well, so this solid cannot be hollowed by an "
                      "offset, whatever the thickness",
                    cause);
                return false;
            }
            catch (Base::Exception& e)
            {
                CORE_ERROR("[Thickness] {0}: {1}", GetName(), e.what());
                return false;
            }
            ++next;
        }

        if (hollowed.empty()) {
            CORE_ERROR("[Thickness] {0}: nothing was hollowed out", GetName());
            return false;
        }

        // The hollowed solids are one body again.
        Part::TopoShape result;
        if (hollowed.size() > 1) {
            result.makeElementFuse(hollowed);
        }
        else {
            result = hollowed.front();
        }
        // As in the fillet and the chamfer: the element map has to travel with the
        // shape, or the references of whatever comes next are lost.
        setResultShape(result);
        getPreviewShape() = *topoShape;
        return true;
	}
}
