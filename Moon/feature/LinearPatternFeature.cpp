#include "LinearPatternFeature.h"

#include "core/component/TopoShapeActor.h"
#include "TopoShape.h"
#include "ElementNamingUtils.h"
#include "TransformHelper.h"
#include "Sketcher/SketcherObj.h"
#include "core/log.h"

#include <algorithm>
#include <vector>

#include <Precision.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>
#include <tracy/Tracy.hpp>

namespace MOON
{
	LinearPatternFeature::LinearPatternFeature(const std::string& p_name)
		: Feature3D(p_name, "LinearPattern")
	{
	}

	LinearPatternFeature::~LinearPatternFeature()
	{
	}

	bool LinearPatternFeature::applySketchDirection(int p_direction, int p_axis)
	{
		SketcherObj* sketch = findBaseSketch();
		if (sketch == nullptr) {
			return false;
		}

		Base::Vector3d axis;
		switch (p_axis) {
			case DirectionSketchX:
				axis = sketch->getPlaneXAxis();
				break;
			case DirectionSketchY:
				axis = sketch->getPlaneYAxis();
				break;
			default: {
				double normal[3] = {0.0, 0.0, 1.0};
				sketch->getPlaneNormal(normal);
				axis = Base::Vector3d(normal[0], normal[1], normal[2]);
				break;
			}
		}
		if (axis.Length() < Precision::Confusion()) {
			return false;
		}

		const gp_Dir dir(axis.x, axis.y, axis.z);
		if (p_direction == 2) {
			direction2 = dir;
			directionType2 = p_axis;
		}
		else {
			direction = dir;
			directionType = p_axis;
		}
		return true;
	}

	bool LinearPatternFeature::execute()
	{
		try {
			Part::TopoShape baseShape = getBaseTopoShape();
			if (baseShape.isNull()) {
				CORE_ERROR(
					"[LinearPattern] {0}: there is no shape below it to pattern",
					GetName());
				return false;
			}

			// The directions belong to the sketch the body was built from, so they are
			// read again here instead of being trusted from the last time the panel set
			// them: turning that sketch has to turn the pattern with it.
			if (directionType != DirectionPickedEdge) {
				applySketchDirection(1, directionType);
			}
			if (directionType2 != DirectionPickedEdge) {
				applySketchDirection(2, directionType2);
			}

			const int count1 = std::max(occurrences, 1);
			const int count2 = std::max(occurrences2, 1);

			// One step per direction, following FreeCAD: an extent is shared by the
			// instances that sit between its ends, a spacing is the step itself.
			gp_Vec step1(0.0, 0.0, 0.0);
			if (count1 > 1) {
				const double extent = static_cast<double>(length);
				if (std::fabs(extent) < Precision::Confusion()) {
					CORE_ERROR(
						"[LinearPattern] {0}: the pattern length is null", GetName());
					return false;
				}
				const double distance = dimensionMode == Spacing
					? extent
					: extent / (count1 - 1);
				gp_Dir axis = direction;
				if (reverse) {
					axis.Reverse();
				}
				step1 = gp_Vec(axis) * distance;
			}

			gp_Vec step2(0.0, 0.0, 0.0);
			if (count2 > 1) {
				const double extent = static_cast<double>(length2);
				if (std::fabs(extent) < Precision::Confusion()) {
					CORE_ERROR(
						"[LinearPattern] {0}: the second pattern length is null", GetName());
					return false;
				}
				const double distance = dimensionMode2 == Spacing
					? extent
					: extent / (count2 - 1);
				gp_Dir axis = direction2;
				if (reverse2) {
					axis.Reverse();
				}
				step2 = gp_Vec(axis) * distance;
			}

			ZoneScopedN("LinearPattern");
			// Every combination of the two directions. The identity comes first: it
			// is the original, which the base holds already and which every mode
			// keeps.
			std::vector<gp_Trsf> transformations;
			transformations.reserve(
				static_cast<size_t>(count1) * static_cast<size_t>(count2));
			transformations.push_back(gp_Trsf());
			for (int i = 0; i < count1; ++i) {
				for (int j = 0; j < count2; ++j) {
					if (i == 0 && j == 0) {
						continue;
					}
					gp_Trsf translation;
					translation.SetTranslation(step1 * i + step2 * j);
					transformations.push_back(translation);
				}
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
				// The preview shows the copies the pattern adds, not the finished body
				// they are fused with: that one is already on screen.
				std::vector<Part::TopoShape> copies(instances.begin() + 1, instances.end());
				preview.makeElementCompound(
					copies,
					nullptr,
					Part::TopoShape::SingleShapeCompoundCreationPolicy::returnShape);
				result.makeElementFuse(instances);
			}
			if (result.isNull()) {
				CORE_ERROR(
					"[LinearPattern] {0}: fusing the instances produced no shape",
					GetName());
				return false;
			}

			getPreviewShape() = preview.isNull() ? result : preview;
			setResultShape(result);
			CORE_INFO(
				"[LinearPattern] {0}: {1} by {2} instance(s)",
				GetName(), count1, count2);
			return true;
		}
		catch (const Standard_Failure& e) {
			CORE_ERROR("[LinearPattern] {0}: {1}", GetName(), e.GetMessageString());
		}
		catch (const Base::Exception& e) {
			CORE_ERROR("[LinearPattern] {0}: {1}", GetName(), e.what());
		}
		catch (const std::exception& e) {
			CORE_ERROR("[LinearPattern] {0}: {1}", GetName(), e.what());
		}
		catch (...) {
			CORE_ERROR("[LinearPattern] {0}: unknown error", GetName());
		}
		return false;
	}
}
