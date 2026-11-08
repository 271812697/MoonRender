#pragma once
#include <array>
#include <string>
#include <vector>

#include <Rendering/Data/DescriptorSetLayout.h>

/** The shader-side descriptor binding table for the per-material set, filament's
 * backend::Program::DescriptorSetInfo. Sets 0/1 are engine-wide and live in
 * EngineDescriptorSets.h; only set 2 is material-specific and goes through the binding map. */
namespace Rendering::Data
{
	using DescriptorSetInfo = std::array<
		std::vector<DescriptorSetLayout::Binding>, 3>;

	constexpr uint32_t kPerViewDescriptorSet = 0;
	constexpr uint32_t kPerRenderableDescriptorSet = 1;
	constexpr uint32_t kPerMaterialDescriptorSet = 2;
}
