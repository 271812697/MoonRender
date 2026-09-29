#pragma once
#include "feature/Feature.h"
#include "feature/TransformMode.h"

#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <string>

namespace Part
{
	class TopoShape;
}

namespace MOON
{
	/** Turns the shape below into its mirror image and keeps both.
	 *
	 * The shape is reflected across a plane and fused with the original, which is
	 * what the FreeCAD feature this follows does: its list of transformations is the
	 * identity followed by the reflection, so the original is always part of the
	 * result.
	 *
	 * The plane is either one of the planes of the sketch the shape was built from
	 * (the sketch plane itself, or the planes that hold its X or Y axis) or a planar
	 * face picked on the shape below.
	 *
	 * The "feature" mode - mirror only the picked features of the body instead of
	 * the whole of it - is not implemented yet, for the same reason as in the polar
	 * pattern: the feature graph keeps only the result of a feature, not the
	 * additive and subtractive shapes apart.
	 *
	 * It is a Feature3D, like the other features that produce a solid, so that a
	 * feature built on top of the mirror finds its shape here.
	 */
	class MirrorFeature : public Feature3D
	{
	public:
		/** Where the mirror plane comes from. */
		enum PlaneType
		{
			PlaneSketchNormal = 0,  ///< the plane of the sketch itself
			PlaneSketchX = 1,       ///< the plane that holds the X axis of the sketch
			PlaneSketchY = 2,       ///< the plane that holds the Y axis of the sketch
			PlanePickedFace = 3     ///< a planar face picked on the shape below
		};

		MirrorFeature(const std::string& p_name);
		virtual ~MirrorFeature() override;
		virtual bool execute() override;

		/** Puts the mirror plane on one of the planes of the sketch the shape below
		 * was built from.
		 *
		 * The sketch the user is editing is used when nothing in the chain below has
		 * one. \return false when there is no such sketch, in which case the plane
		 * is left as it was.
		 */
		bool applySketchPlane(int p_plane);

		/** Takes the plane from a planar face, which is normally a face of the shape
		 * below. \return false when the face is missing or is not planar. */
		bool applyPlaneFromFace(const Part::TopoShape& p_face);

		int mode = static_cast<int>(TransformMode::Whole);
		/** One of PlaneType: where the mirror plane comes from. */
		int planeType = PlaneSketchNormal;
		/** Mirror plane, in the world of the shape below: its location is a point of
		 * the plane and its main direction is the normal of it. */
		gp_Ax2 plane = gp_Ax2(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0));
	};
}
