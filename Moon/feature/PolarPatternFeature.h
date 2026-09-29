#pragma once
#include "feature/Feature.h"
#include "feature/TransformMode.h"

#include <vector>
#include <gp_Ax1.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <string>

namespace MOON
{
	/** Turns the shape below into a circular pattern.
	 *
	 * The shape is copied as many times as there are occurrences, every copy is
	 * rotated around the pattern axis, and the copies are fused with the original.
	 * That is what the FreeCAD feature this follows does in its "whole" mode.
	 *
	 * Its "feature" mode - pattern the material of the picked features instead of
	 * the whole body - is the default, as it is in FreeCAD: it repeats what those
	 * features added or took away (a hole, a boss, a fillet) on the same base, which
	 * keeps the result a single solid. The "whole" mode duplicates the shape below
	 * the pattern instead, which is what one wants when the pattern is the last
	 * thing a body is made of.
	 *
	 * It is a Feature3D, like the other features that produce a solid: that is what
	 * FeatureBody::getLastBaseFeature() looks for when a new feature asks for the
	 * shape below it, and a pattern that is not one would be passed over - the next
	 * feature would then be built on the shape the pattern was made of.
	 */
	class PolarPatternFeature : public Feature3D
	{
	public:
		/** Where the rotation axis comes from. */
		enum AxisType
		{
			AxisSketchX = 0,
			AxisSketchY = 1,
			AxisSketchNormal = 2,
			AxisPickedEdge = 3
		};

		PolarPatternFeature(const std::string& p_name);
		virtual ~PolarPatternFeature() override;
		virtual bool execute() override;

		/** Puts the axis on one of the axes of the sketch the shape below was built
		 * from, through the origin of that sketch.
		 *
		 * A circular pattern normally turns inside the plane of that sketch - the
		 * normal axis is the default - which is what this feature starts from. The
		 * sketch the user is editing is used when nothing in the chain below has
		 * one.
		 *
		 * \return false when there is no such sketch, in which case the axis is
		 *         left as it was.
		 */
		bool applySketchAxis(int p_axis);

		int mode = static_cast<int>(TransformMode::Feature);
		/** The features whose material is patterned in the "feature" mode. Empty in
		 * the whole shape mode, which patterns the shape below as a whole. */
		std::vector<Feature*> originals;
		/** One of AxisType: which axis the copies turn around. */
		int axisType = AxisSketchNormal;
		/** Instances in the pattern, the original included. */
		int occurrences = 3;
		/** Total angle the pattern spans, in degrees. A full turn is shared by the
		 * instances, a smaller angle is measured from the first to the last one. */
		float angle = 360.0f;
		/** Run the pattern the other way around the axis. */
		bool reverse = false;
		/** Rotation axis, in the world of the shape below. Starts on the world Z
		 * axis and is replaced by one of the sketch axes, or by the picked edge,
		 * as soon as the panel knows which one the pattern turns around. */
		gp_Ax1 axis = gp_Ax1(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0));
	};
}
