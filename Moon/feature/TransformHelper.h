#pragma once
#include <gp_Trsf.hxx>

#include <vector>

namespace Part
{
	class TopoShape;
}

namespace MOON
{
	class Feature;

	/** Builds the result of a transform feature that runs in its "feature" mode.
	 *
	 * The shape of the feature the transform sits on is the base, and the features
	 * picked for the pattern contribute their own material - what each of them added
	 * to, or took away from, that base. Only that material is transformed: every copy
	 * of it is fused or cut back onto the base, which already holds the original. So
	 * a pattern of a hole digs the same hole again elsewhere, and a pattern of a boss
	 * grows it again, on one and the same body.
	 *
	 * This is what FreeCAD's PartDesign::Transformed does in its Mode::Features, and
	 * it is the reason the result stays a single solid instead of falling apart into
	 * copies of the whole shape.
	 *
	 * \param p_transformations the identity first, followed by one transformation
	 *        per further instance. The identity is not applied again, because the
	 *        material of the original is part of the base already.
	 * \param p_previewTools receives the material that is being repeated - the tool
	 *        of every original together with its transformed copies - which is what
	 *        the panel shows as the preview. Drawing the finished shape instead
	 *        would hide a pattern of holes inside the body it digs into.
	 * \return the shape of the feature, or a null one when there is nothing to
	 *         transform (which the caller reports).
	 */
	Part::TopoShape BuildFeatureModeResult(
		Feature& p_owner,
		const std::vector<Feature*>& p_originals,
		const std::vector<gp_Trsf>& p_transformations,
		Part::TopoShape& p_previewTools
	);
}
