#pragma once

namespace Rendering::HAL
{
	/** Marks the construction of the *backend* object: the one that makes the GL name, in the slot
	 * a handle names (Filament's createTextureR / createBufferObjectR). The plain constructor of a
	 * front-end object only takes a handle and records the creation; this one runs where the
	 * context is, so it must never be reached from the recording side. */
	struct BackendConstructionTag {};
}
