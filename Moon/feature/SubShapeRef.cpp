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

namespace MOON
{
	Part::TopoShape ResolveSubShapeRef(
		Feature& p_source,
		const std::string& p_reference,
		std::vector<std::string>& p_names)
	{
		auto comp = p_source.GetComponent<Core::ECS::Components::CTopoShape>();
		if (comp == nullptr) {
			return Part::TopoShape();
		}
		Part::TopoShape& baseShape = comp->GetTopoShape();

		// 1) By the names recorded when the reference was last resolved: this is the
		// path that survives a recompute of the source.
		for (const std::string& name : p_names) {
			TopoDS_Shape byName = baseShape.getSubShape(name.c_str(), true);
			if (!byName.IsNull()) {
				CORE_INFO(
					"[TopoRef] {0}: '{1}' resolved by name '{2}'",
					p_source.GetName(), p_reference, name);
				return Part::TopoShape(byName);
			}
		}
		if (!p_names.empty()) {
			CORE_WARN(
				"[TopoRef] {0}: none of the {1} name(s) of reference '{2}' is in the "
				"shape any more; falling back to its index",
				p_source.GetName(), p_names.size(), p_reference);
		}

		// 2) By index, the historical path. The reference is "<Type>_<index>" and the
		// index inside the shape is 1 based.
		const bool isFace = p_reference.rfind("Face", 0) == 0;
		const std::string typeName = isFace ? "Face" : "Edge";
		const TopAbs_ShapeEnum type = isFace ? TopAbs_FACE : TopAbs_EDGE;
		int index = 0;
		try {
			index = std::stoi(p_reference.substr(5));
		}
		catch (const std::exception&) {
			CORE_ERROR(
				"[TopoRef] {0}: cannot read an index out of the reference '{1}'",
				p_source.GetName(), p_reference);
			return Part::TopoShape();
		}
		Part::TopoShape result = baseShape.getSubTopoShape(type, index + 1);

		// Remember every name this element currently has, so a later resolve can be
		// name based again even if a recompute moves the sub-shape elsewhere.
		const Data::IndexedName element
			= Data::IndexedName::fromConst(typeName.c_str(), index + 1);
		p_names.clear();
		for (const std::pair<Data::MappedName, Data::ElementIDRefs>& candidate
			: baseShape.getElementMappedNames(element, false)) {
			p_names.push_back(candidate.first.toString());
		}
		// Longest first: the most specific name is also the one that survives the
		// most operations downstream.
		std::sort(p_names.begin(), p_names.end(),
			[](const std::string& p_left, const std::string& p_right)
			{
				return p_left.size() > p_right.size();
			});

		if (!p_names.empty()) {
			std::string joined;
			for (const std::string& name : p_names) {
				joined += joined.empty() ? name : (" | " + name);
			}
			CORE_INFO(
				"[TopoRef] {0}: '{1}' resolved by index {2}, captured name(s) '{3}'",
				p_source.GetName(), p_reference, index, joined);
		}
		else {
			CORE_WARN(
				"[TopoRef] {0}: the shape carries no mapped name for {1}_{2}; the "
				"reference stays index based and may break on recompute",
				p_source.GetName(), typeName, index);
		}
		return result;
	}
}
