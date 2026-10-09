#pragma once
#include "feature/Feature.h"
#include <Maths/FVector3.h>
#include <string>
#include <vector>

namespace MOON
{
	/** A datum plane: a parametric reference plane.
	 *
	 * Like the datum line it does not modify the shape below it - it carries a
	 * planar face of its own that later features use as a reference (a sketch on
	 * it, a mirror plane, an up-to-plane stop, ...).
	 *
	 * Where the plane lies is decided by an *attachment*: a mode plus the
	 * sub-shapes of the body that mode works on, the same idea (and the same mode
	 * names) as FreeCAD's plane attachment. On top of it sits a placement offset,
	 * and the face it is drawn with resizes itself to the shape below.
	 */
	class DatumPlaneFeature : public DatumFeature
	{
	public:
		/** How the plane finds its place. The identifier - not the label - is what
		 * a document stores, and it matches FreeCAD's name for the same mode. */
		enum class MapMode
		{
			Deactivated,
			ObjectXY,
			ObjectXZ,
			ObjectYZ,
			FlatFace,
			ThreePoints,
			NormalToEdge
		};

		DatumPlaneFeature(const std::string& p_name);
		virtual ~DatumPlaneFeature() override;
		virtual bool execute() override;

		MapMode mapMode = MapMode::ObjectXY;
		/** Placement on top of the attachment: a shift in the plane's own frame
		 * (x along its axis, y across it, z along its normal) and a turn about the
		 * normal, in degrees. */
		Maths::FVector3 offset{ 0.0f, 0.0f, 0.0f };
		float rotation = 0.0f;
		/** The size of the face the plane is drawn with. Automatic takes it from the
		 * shape below, the way FreeCAD's datum plane sizes itself. */
		bool automaticSize = true;
		float length = 40.0f;
		float width = 40.0f;

		/** The placement the last successful attachment produced, offset not
		 * included: what the panel shows, and what the plane keeps when one of its
		 * references cannot be resolved any more. */
		Maths::FVector3 origin{ 0.0f, 0.0f, 0.0f };
		Maths::FVector3 normal{ 0.0f, 0.0f, 1.0f };
		Maths::FVector3 xAxis{ 1.0f, 0.0f, 0.0f };

		/** The modes, in the order a panel lists them. */
		static const std::vector<MapMode>& allMapModes();
		/** The identifier a document stores ("FlatFace", "ObjectXY", ...). */
		static const char* mapModeName(MapMode p_mode);
		/** The text a panel shows ("Flat face", "Object XY", ...). */
		static const char* mapModeLabel(MapMode p_mode);
		static MapMode mapModeFromName(
			const std::string& p_name,
			MapMode p_fallback = MapMode::ObjectXY);
		/** How many sub-shapes the mode works on: none, one, or three points. */
		int requiredReferenceCount() const;

	private:
		/** The attachment of the current mode.
		 * @return false when the references do not describe it - wrong kind, too
		 * few of them, or a degenerate configuration - which leaves the plane at
		 * the placement it had. */
		bool resolveAttachment(
			Maths::FVector3& p_origin,
			Maths::FVector3& p_normal,
			Maths::FVector3& p_xAxis);
		/** The origin of the shape below: what the "parallel to a global plane"
		 * modes pass through. The world origin when there is nothing below. */
		Maths::FVector3 baseOrigin() const;
	};
}
