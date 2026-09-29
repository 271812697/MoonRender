#include "PolarPatternFeature.h"

#include "core/component/TopoShapeActor.h"
#include "TopoShape.h"
#include "TopoShapeOpCode.h"
#include "ElementNamingUtils.h"
#include "TransformHelper.h"
#include "Sketcher/SketcherObj.h"
#include "core/log.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <Precision.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <tracy/Tracy.hpp>

namespace MOON
{
	namespace
	{
		constexpr double kPi = 3.14159265358979323846;
	}

	PolarPatternFeature::PolarPatternFeature(const std::string& p_name)
		: Feature3D(p_name, "PolarPattern")
	{
	}

	PolarPatternFeature::~PolarPatternFeature()
	{
	}

	bool PolarPatternFeature::applySketchAxis(int p_axis)
	{
		SketcherObj* sketch = findBaseSketch();
		if (sketch == nullptr) {
			return false;
		}

		const Base::Vector3d origin = sketch->getPlaneOrigin();
		Base::Vector3d direction;
		switch (p_axis) {
			case AxisSketchX:
				direction = sketch->getPlaneXAxis();
				break;
			case AxisSketchY:
				direction = sketch->getPlaneYAxis();
				break;
			default: {
				double normal[3] = {0.0, 0.0, 1.0};
				sketch->getPlaneNormal(normal);
				direction = Base::Vector3d(normal[0], normal[1], normal[2]);
				break;
			}
		}
		if (direction.Length() < Precision::Confusion()) {
			return false;
		}

		axis = gp_Ax1(
			gp_Pnt(origin.x, origin.y, origin.z),
			gp_Dir(direction.x, direction.y, direction.z));
		axisType = p_axis;
		return true;
	}

	bool PolarPatternFeature::execute()
	{
		try {
			Part::TopoShape baseShape = getBaseTopoShape();
			if (baseShape.isNull()) {
				CORE_ERROR(
					"[PolarPattern] {0}: there is no shape below it to pattern",
					GetName());
				return false;
			}

			// The axis belongs to the sketch the body was built from, so it is read
			// again here instead of being trusted from the last time the panel set
			// it: moving that sketch has to move the pivot with it.
			if (axisType != AxisPickedEdge) {
				applySketchAxis(axisType);
			}

			gp_Ax1 rotationAxis = axis;
			if (reverse) {
				rotationAxis.Reverse();
			}

			const int count = std::max(occurrences, 1);
			double step = 0.0;
			if (count > 1) {
				const double extent = std::fabs(static_cast<double>(angle));
				if (extent < 1.0e-6) {
					CORE_ERROR(
						"[PolarPattern] {0}: the pattern angle is null", GetName());
					return false;
				}
				// A full turn is shared by the instances (three of them sit 120
				// degrees apart), a smaller extent runs from the first to the last.
				const double gap = extent >= 360.0 - 1.0e-6
					? extent / count
					: extent / (count - 1);
				step = gap * kPi / 180.0;
				if (step < Precision::Angular()) {
					CORE_ERROR(
						"[PolarPattern] {0}: the pattern angle is too small for {1} "
						"occurrences",
						GetName(), count);
					return false;
				}
			}

			ZoneScopedN("PolarPattern");
			// The identity comes first: it is the original, which the base already
			// holds and which every mode keeps.
			std::vector<gp_Trsf> transformations;
			transformations.reserve(count);
			transformations.push_back(gp_Trsf());
			for (int i = 1; i < count; ++i) {
				gp_Trsf rotation;
				rotation.SetRotation(rotationAxis, step * i);
				transformations.push_back(rotation);
			}

			Part::TopoShape result;
			Part::TopoShape preview;
			if (mode == static_cast<int>(TransformMode::Feature)) {
				result = BuildFeatureModeResult(
					*this, originals, transformations, preview);
			}
			else if (transformations.size() == 1) {
				result = baseShape;
			}
			else {
				std::vector<Part::TopoShape> instances;
				instances.reserve(transformations.size());
				instances.push_back(baseShape);
				for (size_t i = 1; i < transformations.size(); ++i) {
					// The index keeps the elements of the copies apart: without it
					// every copy would name its edges exactly like the original.
					instances.push_back(baseShape.makeElementTransform(
						transformations[i],
						Data::indexSuffix(static_cast<int>(i) + 1).c_str()));
				}
				// The preview shows the shapes the pattern adds, not the finished
				// body they are fused with: the body itself is already on screen.
				std::vector<Part::TopoShape> copies(instances.begin() + 1, instances.end());
				preview.makeElementCompound(
					copies,
					nullptr,
					Part::TopoShape::SingleShapeCompoundCreationPolicy::returnShape);
				result.makeElementFuse(instances);
			}
			if (result.isNull()) {
				CORE_ERROR(
					"[PolarPattern] {0}: fusing the instances produced no shape",
					GetName());
				return false;
			}

			getPreviewShape() = preview.isNull() ? result : preview;
			setResultShape(result);
			CORE_INFO(
				"[PolarPattern] {0}: {1} instance(s) over {2} degrees",
				GetName(), count, angle);
			return true;
		}
		catch (const Standard_Failure& e) {
			CORE_ERROR("[PolarPattern] {0}: {1}", GetName(), e.GetMessageString());
		}
		catch (const Base::Exception& e) {
			CORE_ERROR("[PolarPattern] {0}: {1}", GetName(), e.what());
		}
		catch (const std::exception& e) {
			CORE_ERROR("[PolarPattern] {0}: {1}", GetName(), e.what());
		}
		catch (...) {
			CORE_ERROR("[PolarPattern] {0}: unknown error", GetName());
		}
		return false;
	}
}
