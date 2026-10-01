#include "TransformHelper.h"

#include "Feature.h"
#include "TopoShape.h"
#include "ElementNamingUtils.h"
#include "core/log.h"

#include <cstddef>

namespace MOON
{
	Part::TopoShape BuildFeatureModeResult(
		Feature& p_owner,
		const std::vector<Feature*>& p_originals,
		const std::vector<gp_Trsf>& p_transformations,
		Part::TopoShape& p_previewTools)
	{
		p_previewTools = Part::TopoShape();
		if (p_originals.empty()) {
			CORE_ERROR(
				"[Transform] {0}: no feature was picked to transform", p_owner.GetName());
			return Part::TopoShape();
		}

		Part::TopoShape result = p_owner.getBaseTopoShape();
		if (result.isNull()) {
			CORE_ERROR(
				"[Transform] {0}: there is no shape below it to transform",
				p_owner.GetName());
			return Part::TopoShape();
		}
		if (p_transformations.size() < 2) {
			// A single instance is the base as it stands.
			return result;
		}

		// Everything that is being repeated, for the preview: the material of every
		// original and the copies made of it.
		std::vector<Part::TopoShape> preview;

		for (Feature* original : p_originals) {
			if (original == nullptr) {
				continue;
			}

			Part::TopoShape tool = original->getToolShape();
			if (tool.isNull()) {
				CORE_ERROR(
					"[Transform] {0}: '{1}' has no material of its own to transform; "
					"a sketch, a datum or another transform cannot be patterned",
					p_owner.GetName(), original->GetName());
				return Part::TopoShape();
			}
			// The tool is taken as the consumer of that feature sees it, exactly like
			// the shapes the features hand to each other.
			Feature::applyWorldTransform(*original, tool);
			preview.push_back(tool);

			std::vector<Part::TopoShape> operands;
			operands.reserve(p_transformations.size());
			// The base goes first, both for the fuse and for the cut: it is the solid
			// the copies are added to or taken away from.
			operands.push_back(result);
			for (size_t i = 1; i < p_transformations.size(); ++i) {
				// The index keeps the elements of the copies apart: without it every
				// copy would name its edges exactly like the ones they came from.
				operands.push_back(tool.makeElementTransform(
					p_transformations[i],
					Data::indexSuffix(static_cast<int>(i) + 1).c_str()));
				preview.push_back(operands.back());
			}

			if (original->isToolSubtractive()) {
				result.makeElementCut(operands);
			}
			else {
				result.makeElementFuse(operands);
			}
			if (result.isNull()) {
				CORE_ERROR(
					"[Transform] {0}: '{1}' could not be applied to the shape below it",
					p_owner.GetName(), original->GetName());
				return Part::TopoShape();
			}
		}

		if (!preview.empty()) {
			p_previewTools.makeElementCompound(
				preview,
				nullptr,
				Part::TopoShape::SingleShapeCompoundCreationPolicy::returnShape);
		}
		return result;
	}
}
