#include "feature/DatumLineFeature.h"
#include "core/component/CTopoShape.h"
#include "core/log.h"
#include "TopoShape.h"
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <TopoDS_Edge.hxx>
#include <gp_Pnt.hxx>
#include <Precision.hxx>
#include <cmath>

namespace MOON
{
	DatumLineFeature::DatumLineFeature(const std::string& p_name)
		: DatumFeature(p_name, "DatumLine")
	{
	}

	DatumLineFeature::~DatumLineFeature()
	{
	}

	bool DatumLineFeature::execute()
	{
		try {
			Maths::FVector3 dir = direction;
			const double dirLen = std::sqrt(
				static_cast<double>(dir.x) * dir.x
				+ static_cast<double>(dir.y) * dir.y
				+ static_cast<double>(dir.z) * dir.z
			);
			if (dirLen < Precision::Confusion()) {
				dir = Maths::FVector3(0.0f, 0.0f, 1.0f);
			}
			else {
				const double inv = 1.0 / dirLen;
				dir.x = static_cast<float>(dir.x * inv);
				dir.y = static_cast<float>(dir.y * inv);
				dir.z = static_cast<float>(dir.z * inv);
			}

			// The placement is the center of the finite display segment.
			const double halfLen = static_cast<double>(length) * 0.5;
			gp_Pnt p0(
				origin.x - dir.x * halfLen,
				origin.y - dir.y * halfLen,
				origin.z - dir.z * halfLen
			);
			gp_Pnt p1(
				origin.x + dir.x * halfLen,
				origin.y + dir.y * halfLen,
				origin.z + dir.z * halfLen
			);

			BRepBuilderAPI_MakeEdge mkEdge(p0, p1);
			if (!mkEdge.IsDone()) {
				return false;
			}

			const TopoDS_Shape edge = mkEdge.Edge();
			// Unlike the other features this one hands on a bare shape, so it carries
			// no mapped names: a datum has nothing it was derived from and nothing
			// above it references by name yet. Going through setResultShape() would
			// only warn about that on every recompute.
			topoShape->setShape(edge);
			getPreviewShape().setShape(edge);
			return true;
		}
		catch (const Standard_Failure& e) {
			CORE_ERROR("DatumLine execute failed: {}", e.GetMessageString());
			return false;
		}
	}
}
