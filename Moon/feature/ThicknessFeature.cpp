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

namespace MOON {
	ThicknessFeature::ThicknessFeature(const std::string& p_name) :Feature3D(p_name, "Thickness")
	{
	}
	ThicknessFeature::~ThicknessFeature()
	{
	}
	bool ThicknessFeature::execute()
	{
        double tol = Precision::Confusion();
        double thickness = (reverse ? -1. : 1.) * thickNessValue;
        int join = joinType;
        if (join == 1) {
            join = 2;
        }

        if (fabs(thickness) > 2 * tol) {
            try
            {
                Part::TopoShape baseShape= getBaseTopoShape();
                Part::TopoShape face = getBaseTopoFaceShape();
                Part::TopoShape shape = baseShape.makeElementThickSolid({face}, thickness, tol, intersection, false, mode, static_cast<Part::JoinType>(join));
                // As in the fillet and the chamfer: the element map has to travel with
                // the shape, or the references of whatever comes next are lost.
                setResultShape(shape);
                getPreviewShape() = *topoShape;
                return true;
            }
            catch (Standard_Failure& e)
            {
                CORE_ERROR(e.GetMessageString());
                return false;
            }
            return false;
        }
        return false;
	}
}
