#pragma once
#include <array>
#include <string>
#include <vector>

#include <Rendering/Data/DescriptorSetLayout.h>

/** The shader-side descriptor binding table, filament's backend::Program::DescriptorSetInfo:
 * one entry per descriptor set, each carrying the bindings that set exposes. Sets 0/1/2 are
 * per-view, per-renderable and per-material. The OpenGLProgram binding map turns each
 * (set, binding) into the GL slot the shader is initialized with. */
namespace Rendering::Data
{
	using DescriptorSetInfo = std::array<
		std::vector<DescriptorSetLayout::Binding>, 3>;

	constexpr uint32_t kPerViewDescriptorSet = 0;
	constexpr uint32_t kPerRenderableDescriptorSet = 1;
	constexpr uint32_t kPerMaterialDescriptorSet = 2;
}
