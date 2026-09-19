#pragma once
#include <string>
#include <vector>

namespace Part
{
	class TopoShape;
}

namespace MOON
{
	class Feature;

	/** Resolves a "<Type>_<index>" reference against the *current* shape of
	 * p_source.
	 *
	 * That is the form the task panels store when the user picks an edge or a face,
	 * and it names a position in the enumeration of the shape - which a recompute is
	 * free to change, and that is what used to break a reference.
	 *
	 * p_names holds the mapped names the element was seen to have. An element can
	 * carry several (the one its creator gave it plus the ones later operations
	 * added), and which of them survives depends on how much history the shape has
	 * when the reference is resolved, so every one of them is tried first. When none
	 * of them is in the shape any more the index is used as the fallback, and the
	 * names that index has now are written back into p_names, so the next resolve
	 * can be name based again.
	 *
	 * Shared by Feature (a feature referencing the feature below it) and by the
	 * sketch (external geometry referencing another feature).
	 */
	Part::TopoShape ResolveSubShapeRef(
		Feature& p_source,
		const std::string& p_reference,
		std::vector<std::string>& p_names);
}
