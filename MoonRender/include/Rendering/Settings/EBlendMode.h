#pragma once
#include <cstdint>

namespace Rendering::Settings
{
	/** How a material's colour reaches the framebuffer - filament's BlendMode.
	 *
	 * The renderer turns this into blend factors in ABaseRenderer::DrawEntity; a material's
	 * "blendable" flag is derived from it (anything but opaque blends). "masked" is not here on
	 * purpose: alpha clipping is a shader decision and blends like opaque.
	 *
	 * The values carry a BLEND_ prefix because windows.h defines OPAQUE and TRANSPARENT as
	 * macros, and those two would otherwise expand inside the enum. */
	enum class EBlendMode : uint8_t
	{
		BLEND_OPAQUE,
		BLEND_TRANSPARENT,
		BLEND_ADD,
		BLEND_MULTIPLY,
		BLEND_SCREEN,
		BLEND_FADE
	};
}
