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
#include "RevolveFeature.h"
#include "core/log.h"
#include <cmath>
#include <exception>
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
#include <tracy/Tracy.hpp>
namespace MOON {
	RevolveFeature::RevolveFeature(const std::string& p_name, int addSubType) :FeatureBaseProfile(p_name, addSubType==0? "Revolve" : "Groove")
	{
        this->addSubType = addSubType;
	}
	RevolveFeature::~RevolveFeature()
	{
	}
	bool RevolveFeature::execute()
	{
        try {
            Part::TopoShape face = getProfileFace();
            Part::TopoShape baseShape;
            if (m_baseFeature) {
                baseShape = getBaseTopoShape();
            }
            gp_Ax1 raxis = axis;
            if (reverse) {
                raxis.Reverse();
            }
            constexpr double kPi = 3.14159265358979323846;
            constexpr double kTwoPi = 2.0 * kPi;
            // Use double for the sweep angle: the old float conversion made a
            // full 360 degree turn slightly larger than 2*pi, which made the
            // start/end seam self-intersect and OCC returned only overlapping
            // faces.
            double radAngle = static_cast<double>(angle) * kPi / 180.0;
            if (std::fabs(std::fabs(radAngle) - kTwoPi) < 1.0e-9) {
                radAngle = radAngle > 0.0 ? kTwoPi : -kTwoPi;
            }
            Part::TopoShape revolve;
            {
                ZoneScopedN("Revolve");
                revolve = face.makeElementRevolve(raxis, radAngle, "Part::FaceMakerBullseye");
                getPreviewShape() = revolve;
            }

            Part::TopoShape resShape;
            if (!baseShape.isNull()) {
                ZoneScopedN("makeBoolen");
                if (addSubType == 0) {
                    resShape = baseShape.makeElementFuse(revolve);
                }
                else if (addSubType == 1) {
                    resShape = baseShape.makeElementCut(revolve);
                }
            }
            else {
                resShape = revolve;
            }

            setResultShape(resShape);
            return true;
        }
        catch (const Standard_Failure& e) {
            CORE_ERROR("Revolve failed: {}", e.GetMessageString());
        }
        catch (const Base::Exception& e) {
            CORE_ERROR("Revolve failed: {}", e.what());
        }
        catch (const std::exception& e) {
            CORE_ERROR("Revolve failed: {}", e.what());
        }
        catch (...) {
            CORE_ERROR("Revolve failed: unknown error");
        }
        return false;
	}
}
