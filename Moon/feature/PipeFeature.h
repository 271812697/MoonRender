#pragma once
#include "feature/FeatureBaseProfile.h"
#include <Maths/FVector3.h>
#include <string>
#include <vector>

namespace MOON
{
	/** A pipe: the profile of a sketch swept along a path.
	 *
	 * The path - the *spine*, in FreeCAD's words - is another feature, usually a
	 * path sketch whose wire is the path, or a sub-shape of one (an edge picked on
	 * the body). The swept solid is fused into the shape below (an additive pipe) or
	 * cut out of it (a subtractive pipe), the way FreeCAD's AdditivePipe and
	 * SubtractivePipe do it.
	 *
	 * The path references live in the feature's own subValues block: that is the
	 * block a document already stores references and their names in, and they are
	 * resolved against the *spine* feature rather than against the shape below.
	 */
	class PipeFeature : public FeatureBaseProfile
	{
	public:
		/** How the profile is carried along the path (FreeCAD's "Mode"). */
		enum class Mode
		{
			Standard,
			Fixed,
			Frenet,
			Binormal
		};
		/** What happens where two segments of the path meet (FreeCAD's "Transition"). */
		enum class Transition
		{
			Transformed,
			RightCorner,
			RoundCorner
		};

		/** @param p_addSubType 0 for a pipe that adds material, 1 for one that takes
		 * it away. */
		PipeFeature(const std::string& p_name, int p_addSubType);
		virtual ~PipeFeature() override;
		virtual bool execute() override;
		/** The material this pipe added or took away: what a pattern transforms. */
		virtual Part::TopoShape getToolShape() override;
		virtual bool isToolSubtractive() const override;

		int addSubType = 0;
		Mode mode = Mode::Standard;
		Transition transition = Transition::Transformed;
		/** The direction the profile is kept parallel to in the Binormal mode. */
		Maths::FVector3 binormal{ 0.0f, 0.0f, 1.0f };
		/** The feature the path lives in. */
		Feature* spineFeature = nullptr;
		/** The swept solid, on its own (the preview and what a pattern sees). */
		Part::TopoShape toolShape;

		static const std::vector<Mode>& allModes();
		static const char* modeName(Mode p_mode);
		static const char* modeLabel(Mode p_mode);
		static Mode modeFromName(const std::string& p_name, Mode p_fallback);
		static const std::vector<Transition>& allTransitions();
		static const char* transitionName(Transition p_transition);
		static const char* transitionLabel(Transition p_transition);
		static Transition transitionFromName(
			const std::string& p_name,
			Transition p_fallback);

	private:
		/** The face the profile sketch produced, its pose applied. */
		Part::TopoShape resolveProfileShape();
		/** The path, as one wire: the references the feature holds resolved against
		 * the spine feature, or the whole shape of it when there are none. */
		Part::TopoShape resolveSpineWire();
	};
}
