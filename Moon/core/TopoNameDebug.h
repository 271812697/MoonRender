#pragma once

namespace Part
{
	class TopoShape;
}

namespace MOON
{
	/** Logs the element names a shape carries, one line per shape.
	 *
	 * Used while bringing topological naming in: the log then shows the chain
	 * ("sketch: Edge1 -> g0" after a sketch, "pad: Edge3 -> g0;:M;XTR" after a
	 * pad), which is what a reference has to be able to follow across a
	 * recompute. Shapes are not const: the element map is built lazily, so asking
	 * for it has to be able to create it. */
	void LogTopoElementNames(Part::TopoShape& p_shape, const char* p_tag);
}
