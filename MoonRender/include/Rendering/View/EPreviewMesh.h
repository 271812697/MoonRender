#pragma once
#include <cstdint>

namespace Editor::Panels
{
	/** What a material preview shows the material on.
	 *
	 * The sphere is the neutral default - it shows curvature and a normal map over the whole
	 * hemisphere. The cube and the plane are for materials whose UV layout, tiling or planar
	 * orientation matter more than the curvature. */
	enum class EPreviewMesh : uint8_t
	{
		SPHERE,
		CUBE,
		PLANE
	};
}
