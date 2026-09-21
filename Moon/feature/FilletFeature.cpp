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
#include "FilletFeature.h"
#include "core/log.h"
#include <gp_Pln.hxx>
#include <BRepTools.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <gp_Vec.hxx>
#include <gp_Dir.hxx>
#include <GeomAbs_Shape.hxx>
#include <ShapeFix_ShapeTolerance.hxx>
#include <BRepAlgo.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <ShapeAnalysis_Surface.hxx>
#include <BRepLProp_SLProps.hxx>
#include <Precision.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRep_Tool.hxx>
#include <TopoDS.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <BRepClass3d_SolidClassifier.hxx>

#include <algorithm>

namespace MOON {
	FilletFeature::FilletFeature(const std::string& p_name) :Feature3D(p_name, "Fillet")
	{
	}
	FilletFeature::~FilletFeature()
	{
	}
	bool FilletFeature::execute()
	{
        Part::TopoShape baseShape = getBaseTopoShape();
        
        std::vector<Part::TopoShape> baseEdge=getBaseTopoEdgeShapes();
        std::vector<Part::TopoShape> edges =useAllEdges ? baseShape.getSubTopoShapes(TopAbs_EDGE) : baseEdge ;
        try
        {
            Part::TopoShape resShape(0);
            // The op code is what tags the names of the new fillet faces/edges, so
            // a later operation (or a reference) can tell where they came from.
            resShape.makeElementFillet(baseShape, edges, radius, radius, Part::OpCodes::Fillet);
            if (resShape.isNull()) {
                return false;
            }
            LogTopoElementNames(resShape, "fillet");
            TopTools_ListOfShape aLarg;
            aLarg.Append(baseShape.getShape());
            if (!BRepAlgo::IsValid(aLarg, baseShape.getShape(), Standard_False, Standard_False)) {
                ShapeFix_ShapeTolerance aSFT;
                aSFT.LimitTolerance(
                    baseShape.getShape(),
                    Precision::Confusion(),
                    Precision::Confusion(),
                    TopAbs_SHAPE
                );
            }
            topoShape->setShape(resShape.getShape());

            // Determine the cut direction by comparing volumes: filleting a
            // convex edge removes material (res smaller), filleting a concave
            // edge adds material (res larger). This performs only one boolean
            // cut and avoids relying on an empty-result heuristic.
            GProp_GProps baseProps;
            GProp_GProps resProps;
            BRepGProp::VolumeProperties(baseShape.getShape(), baseProps);
            BRepGProp::VolumeProperties(resShape.getShape(), resProps);
            const double baseVol = baseProps.Mass();
            const double resVol = resProps.Mass();
            const double tol = 1e-7 * std::max(baseVol, 1.0);
            if (resVol < baseVol - tol) {
                getPreviewShape() = baseShape.makeElementCut(resShape);  // removed part
            }
            else if (resVol > baseVol + tol) {
                getPreviewShape() = resShape.makeElementCut(baseShape);  // added part
            }
            else {
                // Volumes (nearly) equal or shape not measurable: fall back to
                // the empty-result heuristic.
                Part::TopoShape preview = baseShape.makeElementCut(resShape);
                if (preview.isNull() || preview.isEmpty()) {
                    getPreviewShape() = resShape.makeElementCut(baseShape);
                }
                else {
                    getPreviewShape() = preview;
                }
            }
            return true;
        }
        catch (Standard_Failure& e)
        {
            CORE_ERROR(e.GetMessageString());
            return false;
        }
        return true;
	}
}
