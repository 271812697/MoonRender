#include "core/TopoNameDebug.h"
#include "TopoShape.h"
#include "MappedElement.h"
#include "core/log.h"

#include <string>

namespace MOON
{

	void LogTopoElementNames(Part::TopoShape& p_shape, const char* p_tag)
	{
		if (p_shape.isNull())
		{
			CORE_INFO("[TopoName] {0}: null shape", p_tag);
			return;
		}
		if (!p_shape.hasElementMap())
		{
			// No map at all: everything downstream can only fall back to the
			// enumeration order of the shape.
			CORE_WARN(
				"[TopoName] {0}: the shape carries no element map, references to it "
				"cannot be traced",
				p_tag);
			return;
		}
        size_t logged = 0;
		static const std::array<TopAbs_ShapeEnum, 3> types = { TopAbs_VERTEX, TopAbs_EDGE, TopAbs_FACE };
		for (int i = 0;i < types.size();i++) {
			int count=p_shape.countSubShapes(types[i]);
			std::string shapeName=p_shape.shapeName(types[i]);
			const char* name = shapeName.c_str();
			for (int k = 1;k <= count;k++) {
				Data::IndexedName elementIndex = Data::IndexedName::fromConst(name, k);
				std::string element=elementIndex.toString();
				for (const std::pair<Data::MappedName, Data::ElementIDRefs>& candidate : p_shape.getElementMappedNames(elementIndex)) {
					CORE_INFO("[TopoName] {0}: {1:<9} -> {2}", p_tag, element, candidate.first.toString());
				}
			}
		}
	}
}
