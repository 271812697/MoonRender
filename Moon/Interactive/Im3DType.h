#pragma once
// Moved into the engine: the geometry/types the overlay draws are part of the rendering core, and
// they never needed Qt. Kept as a forwarding header so the editor's includes stay unchanged while
// the rest of ImRenderer follows (docs/RenderThreadPlan.md §8).
#include <Rendering/Overlay/Im3DType.h>