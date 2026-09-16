#include "core/TopoNameDebug.h"
#include "TopoShape.h"
#include "MappedElement.h"
#include "ElementMap.h"
#include "core/log.h"

#include <string>

namespace MOON
{
	namespace
	{
		/** A shape can carry thousands of named elements (a fillet over a whole
		 * model): log the first ones and the total, not the whole map. */
		constexpr size_t kMaxLoggedNames = 40;
	}

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

		std::string text;
		const std::vector<Data::MappedElement>& elements = p_shape.getElementMap();
		size_t logged = 0;
		for (const Data::MappedElement& element : elements)
		{
			if (logged == kMaxLoggedNames)
			{
				break;
			}
			if (!text.empty())
			{
				text += ", ";
			}
			text += element.index.toString() + " -> " + element.name.toString();
			++logged;
		}
		if (elements.size() > logged)
		{
			text += ", ... (+" + std::to_string(elements.size() - logged) + " more)";
		}
		CORE_INFO("[TopoName] {0}: {1} element(s): {2}", p_tag, elements.size(), text);
	}
}
