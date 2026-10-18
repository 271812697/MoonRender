#pragma once
#include <cstdint>

namespace Rendering::Settings
{
	/** GL binding points of the engine's uniform blocks - filament's descriptor sets, flattened
	 * into GL's single binding-point namespace.
	 *
	 *  - 0 per view (EngineUBO.ovfxh: camera and frame data, written once per view)
	 *  - 1 per view (the reflection probe's own block)
	 *  - 2 per material (the parameter block MoonMatc generates, one range per material)
	 *  - 3 per renderable (ObjectUBO.ovfxh: model and user matrices, written before each draw)
	 *
	 * The .ovfxh headers and the C++ writers both use these numbers, and ShaderLoader checks the
	 * driver's actual block bindings against them when it links a program (see
	 * ValidateParameterBlock), so a drift shows up as one log line at load instead of a wrong
	 * image.
	 *
	 * Filament's descriptor set indices are PER_VIEW = 0, PER_RENDERABLE = 1, PER_MATERIAL = 2;
	 * GL has one namespace, and the reflection block already sits on 1, so the renderable block
	 * moved to 3. */
	constexpr uint32_t kEngineUBOBinding = 0;
	constexpr uint32_t kReflectionUBOBinding = 1;
	constexpr uint32_t kMaterialParameterBinding = 2;
	constexpr uint32_t kObjectUBOBinding = 3;
}
