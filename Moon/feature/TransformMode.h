#pragma once

namespace MOON
{
	/** What a transform feature - a pattern or a mirror - takes from the shape
	 * below it.
	 *
	 * Follows FreeCAD's PartDesign::Transformed::TransformMode: "Whole" uses the
	 * whole shape of the feature below, "Feature" would use only the features
	 * picked from it. Both features share this enum so that the two of them cannot
	 * drift apart on what the numbers mean.
	 */
	enum class TransformMode
	{
		Whole = 0,
		Feature = 1
	};
}
