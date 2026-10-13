#pragma once
#include "feature/FeatureBaseProfile.h"
#include <string>
#include <vector>

namespace MOON
{
	/** A loft: the profile of a sketch carried through the sections that follow it.
	 *
	 * The profile is the first section, the way FreeCAD's loft takes it, and Sections
	 * are the ones it goes through after that - usually sketches on other planes, but a
	 * picked face or a closed edge of the body below works as one too. Each section has
	 * to bring the same number of wires (or of vertices, for a loft that ends in a
	 * point), which is what FreeCAD checks as well: its message is "Sections need to
	 * have the same amount of wires or vertices as the base section".
	 *
	 * Ruled makes the loft run straight from one section to the next instead of
	 * smoothing the whole run through them, Closed joins the last section back to the
	 * first (and then needs no caps), and the solid that comes out is fused into the
	 * shape below or cut out of it - FreeCAD's AdditiveLoft and SubtractiveLoft.
	 *
	 * A section is named the way any other reference is (see PipeFeature): the i-th
	 * entry of the feature's sub-values belongs to the i-th section, and an empty one
	 * means "the whole shape of that feature".
	 */
	class LoftFeature : public FeatureBaseProfile
	{
	public:
		/** @param p_addSubType 0 for a loft that adds material, 1 for one that takes
		 * it away. */
		LoftFeature(const std::string& p_name, int p_addSubType);
		virtual ~LoftFeature() override;
		virtual bool execute() override;
		/** The material this loft added or took away: what a pattern transforms. */
		virtual Part::TopoShape getToolShape() override;
		virtual bool isToolSubtractive() const override;

		int addSubType = 0;
		/** The sections the loft goes through, after the profile. */
		std::vector<Feature*> sections;
		/** Straight from one section to the next instead of smoothed through them. */
		bool ruled = false;
		/** The last section joins back to the first: a ring of sections, no ends. */
		bool closed = false;
		/** The lofted solid, on its own (the preview and what a pattern sees). */
		Part::TopoShape toolShape;

	private:
		/** Every section, the profile first: what the loft is built through. */
		std::vector<Feature*> allSections() const;
		/** The wires of one section - or its vertices, for a section that is a point -
		 * with the pose of its feature applied. */
		std::vector<Part::TopoShape> sectionWires(
			Feature& p_section, const std::vector<std::string>& p_names, int p_index);
		/** The face that caps one end of the loft, out of the wires that end brings. */
		Part::TopoShape endCap(const std::vector<Part::TopoShape>& p_wires);
	};
}
