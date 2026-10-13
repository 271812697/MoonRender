#pragma once
#include "feature/FeatureBaseProfile.h"
#include <Maths/FVector3.h>
#include <string>
#include <vector>

namespace MOON
{
	/** A helix: the profile of a sketch swept along a helix, the way FreeCAD's helix
	 * does it (its AdditiveHelix and SubtractiveHelix).
	 *
	 * The axis is a datum line when one was given, and the normal of the profile's own
	 * plane through the center of the profile otherwise - the spring the profile sits
	 * on. The parameters are FreeCAD's four modes: each of them takes three of
	 * pitch / height / turns / angle / growth and works the others out, which is what
	 * the panel shows as the fields it lets you edit.
	 *
	 * The path is OCC's spiral helix (TopoShape::makeSpiralHelix, the same call FreeCAD
	 * makes), moved onto the axis, and the profile is carried along it with a frenet
	 * pipe. That is very nearly what FreeCAD does, down to the tolerance fix it applies
	 * to the result: a helical sweep is an approximation, and the fusion below needs
	 * the tolerance opened a little for it to succeed.
	 */
	class HelixFeature : public FeatureBaseProfile
	{
	public:
		/** Which three of the five parameters are given; the rest are worked out. The
		 * names are FreeCAD's, and they are what a document stores. */
		enum class Mode
		{
			PitchHeightAngle,
			PitchTurnsAngle,
			HeightTurnsAngle,
			HeightTurnsGrowth
		};

		/** Which line the helix winds around. The first three are the sketch's own axes,
		 * through the origin of the sketch - FreeCAD's "Normal / Vertical / Horizontal
		 * sketch axis", the choices its helix panel offers next to a picked line. */
		enum class AxisMode
		{
			Normal,     // the sketch's normal (out of its plane)
			Vertical,   // the sketch's vertical axis, lying in its plane
			Horizontal, // the sketch's horizontal axis, lying in its plane
			Reference   // a line that was picked: a datum line, or an edge of the body
		};

		/** @param p_addSubType 0 for a helix that adds material, 1 for one that takes
		 * it away. */
		HelixFeature(const std::string& p_name, int p_addSubType);
		virtual ~HelixFeature() override;
		virtual bool execute() override;
		/** The material this helix added or took away: what a pattern transforms. */
		virtual Part::TopoShape getToolShape() override;
		virtual bool isToolSubtractive() const override;

		int addSubType = 0;
		Mode mode = Mode::PitchHeightAngle;
		/** Where the axis comes from. Only one of the first three looks at the profile's
		 * own plane for it; only the last one uses the picked line the panel stored. */
		AxisMode axisMode = AxisMode::Vertical;
		/** The distance one turn advances, and how far the helix runs. The defaults are
		 * FreeCAD's, so a helix made without touching them comes out the same. */
		double pitch = 10.0;
		double height = 30.0;
		/** How many turns it makes, and the angle of a tapered (conical) helix. */
		double turns = 3.0;
		double angle = 0.0;
		/** How much the radius grows per turn, for the mode that takes it directly. */
		double growth = 0.0;
		bool leftHanded = false;
		/** The helix runs the other way along its axis. */
		bool reversed = false;
		/** The datum line the axis is. Left empty, the axis is the profile's own normal
		 * through the center of the profile. */
		Feature* axisFeature = nullptr;
		/** Where the axis ended up when the feature was last built: what the panel shows
		 * and what a document writes down, so a helix comes back on the same axis even
		 * when it was worked out from the profile. */
		Maths::FVector3 axisBase{ 0.0f, 0.0f, 0.0f };
		Maths::FVector3 axisDirection{ 0.0f, 0.0f, 1.0f };
		/** The swept solid, on its own (the preview and what a pattern sees). */
		Part::TopoShape toolShape;

		static const std::vector<Mode>& allModes();
		static const char* modeName(Mode p_mode);
		static const char* modeLabel(Mode p_mode);
		static Mode modeFromName(const std::string& p_name, Mode p_fallback);
		static const std::vector<AxisMode>& allAxisModes();
		static const char* axisModeName(AxisMode p_mode);
		static const char* axisModeLabel(AxisMode p_mode);
		static AxisMode axisModeFromName(const std::string& p_name, AxisMode p_fallback);

	private:
		/** Works the derived parameters out and says whether they can be built with:
		 * the validation, and the arithmetic, of FreeCAD's helix. */
		bool normalizeParameters();
		/** Where the axis is: the datum line when there is one, the profile's normal
		 * through the center of the profile otherwise. */
		bool resolveAxis(const Part::TopoShape& p_profileFace);
		/** The helix itself: a wire OCC builds and this moves onto the axis.
		 * @param p_breakAtTurn where the path is broken into pieces - a whole number of
		 * turns keeps OCC from tripping over its own start point, but a tapered helix
		 * has to be one piece or the solid it sweeps comes out invalid (see FreeCAD's
		 * generateHelixPath, which passes 1000 there for the same reason). */
		Part::TopoShape helixPath(const Part::TopoShape& p_profileFace, double p_breakAtTurn);
	};
}
