#include "core/component/TopoShapeActor.h"
#include "renderer/SceneView.h"
#include "TopoShapeOpCode.h"
#include "core/TopoNameDebug.h"
#include "core/component/CTopoShape.h"
#include "TopoShape.h"
#include "ChamferFeature.h"
#include "core/log.h"

#include <BRepAlgo.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <ShapeFix_ShapeTolerance.hxx>
#include <Precision.hxx>
#include <TopTools_ListOfShape.hxx>

#include <algorithm>

namespace MOON {

	ChamferFeature::ChamferFeature(const std::string& p_name) : Feature3D(p_name, "Chamfer") {
	}

	ChamferFeature::~ChamferFeature() {
	}

	bool ChamferFeature::execute() {
		Part::TopoShape baseShape = getBaseTopoShape();

		std::vector<Part::TopoShape> baseEdge = getBaseTopoEdgeShapes();
		std::vector<Part::TopoShape> edges =
			useAllEdges ? baseShape.getSubTopoShapes(TopAbs_EDGE) : baseEdge;

		// Parameter validation (mirrors FreeCAD PartDesign::Chamfer).
		if (size <= 0.f) {
			CORE_ERROR("Chamfer: Size must be greater than zero");
			return false;
		}
		if (chamferType == 1 && size2 <= 0.f) {
			CORE_ERROR("Chamfer: Size2 must be greater than zero");
			return false;
		}
		if (chamferType == 2 && (angle <= 0.f || angle >= 180.f)) {
			CORE_ERROR("Chamfer: Angle must be greater than 0 and less than 180");
			return false;
		}

		try {
			Part::TopoShape resShape(0);
			resShape.makeElementChamfer(
				baseShape,
				edges,
				static_cast<Part::ChamferType>(chamferType),
				size,
				chamferType == 2 ? angle : size2,   // FreeCAD passes angle as the second radius
				// Tags the names of the chamfer faces/edges with the operation.
				Part::OpCodes::Chamfer,
				flipDirection ? Part::Flip::flip : Part::Flip::none
			);
			LogTopoElementNames(resShape, "chamfer");

			TopTools_ListOfShape aLarg;
			aLarg.Append(baseShape.getShape());
			if (!BRepAlgo::IsValid(aLarg, resShape.getShape(), Standard_False, Standard_False)) {
				ShapeFix_ShapeTolerance aSFT;
				aSFT.LimitTolerance(
					resShape.getShape(),
					Precision::Confusion(),
					Precision::Confusion(),
					TopAbs_SHAPE
				);
			}

			topoShape->setShape(resShape.getShape());

			// Determine the preview cut direction by comparing volumes: a chamfer
			// on a convex edge removes material (res smaller), on a concave edge it
			// adds material (res larger).
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
		catch (Standard_Failure& e) {
			CORE_ERROR(e.GetMessageString());
			return false;
		}
	}

}
