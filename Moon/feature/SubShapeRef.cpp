#include "feature/SubShapeRef.h"
#include "feature/Feature.h"
#include "core/component/CTopoShape.h"
#include "TopoShape.h"
#include "IndexedName.h"
#include "MappedName.h"
#include "ElementMap.h"
#include "core/log.h"

#include <algorithm>
#include <exception>
#include <string>
#include <utility>
#include <vector>

namespace MOON
{
	namespace
	{
		// A mapped name is a chain of tokens: the name the element had when it was
		// created, the codes of the operations that carried it here, and, between
		// them, the markers of the element map that each encode one level of
		// re-encoding (":H" plus the tag and the amount of text it was built from).
		//
		// How many of those levels a name carries is a property of the shape's
		// history, not of the element. The same physical edge of a pad comes out as
		// "g0;SKT;:H:4,E;:H,E;:H,E;:H,E;:G;MAK;:U;MAK;XTR;:H:4,E;:H,E" while the pad
		// extrudes a single wire, and as "...;:U;MAK;XTR" once the profile grew a
		// second wire, because several wires go through a compound and its child
		// maps instead of mapping the draft directly. Comparing the names with the
		// level markers dropped is what lets a reference survive that rebuild.
		void AppendSignificantTokens(
			const std::string& p_name,
			std::vector<std::string>& p_tokens)
		{
			size_t begin = 0;
			while (begin <= p_name.size()) {
				size_t end = p_name.find(';', begin);
				if (end == std::string::npos) {
					end = p_name.size();
				}
				const std::string token = p_name.substr(begin, end - begin);
				const bool isLevelMarker
					= token.size() > 1 && token[0] == ':' && token[1] == 'H';
				if (!token.empty() && !isLevelMarker) {
					p_tokens.push_back(token);
				}
				if (end == p_name.size()) {
					break;
				}
				begin = end + 1;
			}
		}

		/** How far two names agree on the element they describe.
		 *
		 * 2: the same chain of tokens, so the names differ only in their encoding
		 *    levels - the case of a pad that grew a second wire.
		 * 1: one chain is the beginning of the other, so the shorter name stops
		 *    where an operation started to name what it built - the case of a
		 *    reference taken before an operation added its code to the name.
		 * 0: the names have nothing in common.
		 */
		int CompareTokens(
			const std::vector<std::string>& p_left,
			const std::vector<std::string>& p_right)
		{
			if (p_left == p_right) {
				return 2;
			}
			const size_t common = std::min(p_left.size(), p_right.size());
			for (size_t i = 0; i < common; ++i) {
				if (p_left[i] != p_right[i]) {
					return 0;
				}
			}
			return 1;
		}

		/** Look for the element a lost name used to point at.
		 *
		 * Every element of the wanted type is asked for the names it has now and
		 * those are compared to the ones the reference kept. An element whose name
		 * agrees token for token wins over one that only shares a beginning, and
		 * the answer is used only when a single element matches: a reference is
		 * worth nothing if it quietly moves to another edge instead of reporting
		 * that it lost the one it had.
		 *
		 * \return the 1 based index of the element, or 0 when the shape has no
		 *         candidate or more than one.
		 */
		int FindElementByApproximateName(
			const Part::TopoShape& p_shape,
			const char* p_typeName,
			TopAbs_ShapeEnum p_type,
			const std::vector<std::string>& p_names,
			const std::string& p_owner,
			const std::string& p_reference)
		{
			std::vector<std::vector<std::string>> wanted;
			wanted.reserve(p_names.size());
			for (const std::string& name : p_names) {
				std::vector<std::string> tokens;
				AppendSignificantTokens(name, tokens);
				if (!tokens.empty()) {
					wanted.push_back(std::move(tokens));
				}
			}
			if (wanted.empty()) {
				return 0;
			}

			std::vector<int> same;
			std::vector<std::pair<size_t, int>> related;  // token distance, element index
			const unsigned long count = p_shape.countSubShapes(p_type);
			for (unsigned long i = 1; i <= count; ++i) {
				const Data::IndexedName element
					= Data::IndexedName::fromConst(p_typeName, static_cast<int>(i));
				int best = 0;
				size_t bestDistance = 0;
				for (const std::pair<Data::MappedName, Data::ElementIDRefs>& candidate
					: p_shape.getElementMappedNames(element, true)) {
					std::vector<std::string> tokens;
					AppendSignificantTokens(candidate.first.toString(), tokens);
					for (const std::vector<std::string>& name : wanted) {
						const int tier = CompareTokens(name, tokens);
						if (tier == 2) {
							best = 2;
						}
						else if (tier == 1 && best < 2) {
							// One name starts where the other stops. The element
							// whose name is the closest to the one the reference
							// remembers - the fewest tokens apart - is the one it
							// names.
							const size_t distance = tokens.size() > name.size()
								? tokens.size() - name.size()
								: name.size() - tokens.size();
							if (best == 0 || distance < bestDistance) {
								bestDistance = distance;
							}
							best = 1;
						}
					}
				}
				if (best == 2) {
					same.push_back(static_cast<int>(i));
				}
				else if (best == 1) {
					related.emplace_back(bestDistance, static_cast<int>(i));
				}
			}

			if (same.size() == 1) {
				return same.front();
			}
			if (same.empty() && !related.empty()) {
				const auto closest = std::min_element(
					related.begin(), related.end(),
					[](const std::pair<size_t, int>& p_left,
						const std::pair<size_t, int>& p_right)
					{
						return p_left.first < p_right.first;
					});
				const bool unique = std::count_if(
					related.begin(), related.end(),
					[&closest](const std::pair<size_t, int>& p_other)
					{
						return p_other.first == closest->first;
					}) == 1;
				if (unique) {
					return closest->second;
				}
			}
			if (!same.empty() || !related.empty()) {
				CORE_WARN(
					"[TopoRef] {0}: {1} element(s) of '{2}' look like the ones the "
					"reference remembers ({3} of them match token for token); the "
					"reference keeps its index",
					p_owner, same.size() + related.size(), p_reference, same.size());
			}
			return 0;
		}

		/** Remember every name an element has.
		 *
		 * All of them are kept because which one survives depends on how much
		 * history the shape carries at the time a later resolve looks for it.
		 * Longest first: the most specific name is also the one that survives the
		 * most operations downstream.
		 */
		void CaptureElementNames(
			const Part::TopoShape& p_shape,
			const char* p_typeName,
			int p_index,
			std::vector<std::string>& p_names)
		{
			const Data::IndexedName element
				= Data::IndexedName::fromConst(p_typeName, p_index);
			p_names.clear();
			for (const std::pair<Data::MappedName, Data::ElementIDRefs>& candidate
				: p_shape.getElementMappedNames(element, false)) {
				p_names.push_back(candidate.first.toString());
			}
			std::sort(p_names.begin(), p_names.end(),
				[](const std::string& p_left, const std::string& p_right)
				{
					return p_left.size() > p_right.size();
				});
		}
	}

	Part::TopoShape ResolveSubShapeRef(
		Feature& p_source,
		const std::string& p_reference,
		std::vector<std::string>& p_names,
		const std::string& p_owner)
	{
		// The feature that holds the reference is the one worth naming in the log;
		// the shape being searched belongs to the source, which for a feature is
		// the feature below it.
		const std::string& owner = p_owner.empty() ? p_source.GetName() : p_owner;

		// The source is taken as it is seen: a feature that was moved (its actor
		// transform) has moved for whoever references it as well. Keeping the two in step
		// here is what makes the sketch's external geometry follow a moved feature.
		Part::TopoShape baseShape = p_source.getWorldTopoShape();
		if (baseShape.isNull()) {
			return Part::TopoShape();
		}

		// 1) By the names recorded when the reference was last resolved: this is the
		// path that survives a recompute of the source.
		for (const std::string& name : p_names) {
			// Taking the element as a *sub-shape* rather than as a bare shape is what
			// keeps its context: the sub-shape knows the tag of the owner and can
			// regenerate its mapped names lazily, so whatever is built on it (a
			// projection into a sketch, say) can go on naming its own elements.
			Part::TopoShape byName = baseShape.getSubTopoShape(name.c_str(), true);
			if (!byName.isNull()) {
				CORE_INFO(
					"[TopoRef] {0}: '{1}' resolved by name '{2}'",
					owner, p_reference, name);
				return byName;
			}
		}
		if (!p_names.empty()) {
			CORE_WARN(
				"[TopoRef] {0}: none of the {1} name(s) of reference '{2}' is in the "
				"base shape any more",
				owner, p_names.size(), p_reference);
		}

		// 2) By the same names again, but compared without their encoding levels.
		// The element a reference points at usually outlives the exact spelling of
		// its name: what changed below it was how many intermediate shapes the
		// element travelled through, and that only shows up in those levels.
		const bool isFace = p_reference.rfind("Face", 0) == 0;
		const char* typeName = isFace ? "Face" : "Edge";
		const TopAbs_ShapeEnum type = isFace ? TopAbs_FACE : TopAbs_EDGE;
		const int approximated = FindElementByApproximateName(
			baseShape, typeName, type, p_names, owner, p_reference);
		if (approximated > 0) {
			Part::TopoShape result = baseShape.getSubTopoShape(type, approximated);
			CaptureElementNames(baseShape, typeName, approximated, p_names);
			CORE_INFO(
				"[TopoRef] {0}: '{1}' resolved by an approximate name match at {2}_{3}",
				owner, p_reference, typeName, approximated - 1);
			return result;
		}

		// 3) By index, the historical path. The reference is "<Type>_<index>" and
		// the index inside the shape is 1 based.
		int index = 0;
		try {
			index = std::stoi(p_reference.substr(5));
		}
		catch (const std::exception&) {
			CORE_ERROR(
				"[TopoRef] {0}: cannot read an index out of the reference '{1}'",
				owner, p_reference);
			return Part::TopoShape();
		}
		// A recompute is free to leave the shape with fewer elements than it had
		// when the reference was taken, and asking it for one of those used to throw
		// an exception out of here. The reference is simply gone in that case.
		const unsigned long count = baseShape.countSubShapes(type);
		if (index < 0 || static_cast<unsigned long>(index + 1) > count) {
			CORE_ERROR(
				"[TopoRef] {0}: reference '{1}' wants {2}_{3}, but the shape has only "
				"{4} of them",
				owner, p_reference, typeName, index, count);
			return Part::TopoShape();
		}
		Part::TopoShape result = baseShape.getSubTopoShape(type, index + 1, true);

		CaptureElementNames(baseShape, typeName, index + 1, p_names);
		if (!p_names.empty()) {
			std::string joined;
			for (const std::string& name : p_names) {
				joined += joined.empty() ? name : (" | " + name);
			}
			CORE_INFO(
				"[TopoRef] {0}: '{1}' resolved by index {2}, captured name(s) '{3}'",
				owner, p_reference, index, joined);
		}
		else {
			CORE_WARN(
				"[TopoRef] {0}: the shape carries no mapped name for {1}_{2}; the "
				"reference stays index based and may break on recompute",
				owner, typeName, index);
		}
		return result;
	}
}
