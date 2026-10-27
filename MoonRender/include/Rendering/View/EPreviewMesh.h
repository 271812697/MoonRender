#pragma once
#include <cstdint>

namespace Editor::Panels
{
	/** What a material preview shows the material on.
	 *
	 * The sphere is the neutral default - it shows curvature and a normal map over the whole
	 * hemisphere. The ball is the "material ball" most engines use (sphere with a dimple, on a
	 * pedestal): it puts a highlight, a curved surface and a flat-ish base in one view. The cube
	 * and the plane are for materials whose UV layout, tiling or planar orientation matter more
	 * than the curvature. CAD is for the CAD materials: a plate with a boss, carrying the per-face
	 * domain id attribute GeomertySurface reads, so the domain colour path has data to sample. */
	enum class EPreviewMesh : uint8_t
	{
		SPHERE,
		BALL,
		CUBE,
		PLANE,
		CAD
	};
}
