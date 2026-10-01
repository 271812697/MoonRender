#pragma once
#include "feature/Feature.h"
#include "feature/TransformMode.h"

#include <vector>
#include <gp_Dir.hxx>
#include <string>

namespace MOON
{
	/** Turns the shape below into a linear pattern, along one or two directions.
	 *
	 * The shape is copied once per instance, every copy is moved by the steps of the
	 * pattern, and the copies are fused with the original. The instances of the two
	 * directions are combined, so a pattern of 3 by 2 occurrences produces 6 of them,
	 * which is what the FreeCAD feature this follows does.
	 *
	 * A direction is measured in one of two ways, as in FreeCAD. "Extent" makes the
	 * length the span of the whole pattern, from its first instance to its last one,
	 * so the step between two neighbours is length / (occurrences - 1). "Spacing"
	 * takes the length as that step itself.
	 *
	 * The direction is one of the axes of the sketch the shape was built from, or a
	 * straight edge (or the normal of a planar face) picked on the shape below.
	 *
	 * Its "feature" mode - pattern the material of the picked features instead of the
	 * whole body - is the default, as it is in FreeCAD: the copies are fused or cut
	 * back onto the same base. The "whole" mode duplicates the shape below the
	 * pattern instead.
	 *
	 * It is a Feature3D, like the other features that produce a solid, so that a
	 * feature built on top of the pattern finds its shape here.
	 */
	class LinearPatternFeature : public Feature3D
	{
	public:
		/** Where the direction of a pattern comes from. */
		enum DirectionType
		{
			DirectionSketchX = 0,
			DirectionSketchY = 1,
			DirectionSketchNormal = 2,
			DirectionPickedEdge = 3
		};

		/** How the length of a pattern is measured. */
		enum DimensionMode
		{
			Extent = 0,   ///< the length spans the whole pattern
			Spacing = 1   ///< the length is the step between two neighbouring instances
		};

		LinearPatternFeature(const std::string& p_name);
		virtual ~LinearPatternFeature() override;
		virtual bool execute() override;

		/** Puts the direction of one of the two pattern directions on an axis of the
		 * sketch the shape below was built from.
		 *
		 * \param p_direction 1 for the first direction, 2 for the second one.
		 * \param p_axis one of DirectionType.
		 * \return false when there is no such sketch, in which case the direction is
		 *         left as it was.
		 */
		bool applySketchDirection(int p_direction, int p_axis);

		int mode = static_cast<int>(TransformMode::Feature);
		/** The features whose material is patterned in the "feature" mode. Empty in
		 * the whole shape mode, which patterns the shape below as a whole. */
		std::vector<Feature*> originals;

		/** First direction. */
		int directionType = DirectionSketchX;
		int dimensionMode = Extent;
		/** Instances along the first direction, the original included. */
		int occurrences = 2;
		/** Extent of the whole pattern, or the step between two instances, depending
		 * on the mode. */
		float length = 10.0f;
		bool reverse = false;
		/** World direction the instances are moved in. */
		gp_Dir direction = gp_Dir(1.0, 0.0, 0.0);

		/** Second direction, used only when it has more than one occurrence. */
		int directionType2 = DirectionSketchY;
		int dimensionMode2 = Extent;
		int occurrences2 = 1;
		float length2 = 10.0f;
		bool reverse2 = false;
		gp_Dir direction2 = gp_Dir(0.0, 1.0, 0.0);
	};
}
