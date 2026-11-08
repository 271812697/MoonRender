#pragma once
#include <array>
#include <cstdint>

#include <Rendering/Data/DescriptorSetLayout.h>
#include <Rendering/Data/ProgramDescriptorBindings.h>

/** The engine's per-view and per-renderable descriptor sets.
 *
 * These two sets are fixed by the engine, not by a material: every program sees the same
 * names, the same set-local bindings and the same GL slots. Only the per-material set needs
 * a program-specific binding map, and its slots start after the last fixed buffer slot here.
 */
namespace Rendering::Data::EngineDescriptorSets
{
	struct Binding
	{
		const char* name;
		DescriptorSetLayout::Kind kind;
		uint32_t binding;
		uint32_t slot;
	};

	inline constexpr std::array<Binding, 2> kPerViewBindings = {
		Binding{ "EngineUBO", DescriptorSetLayout::Kind::UniformBuffer, 0, 0 },
		Binding{ "ReflectionUBO", DescriptorSetLayout::Kind::UniformBuffer, 1, 1 },
	};

	inline constexpr std::array<Binding, 1> kPerRenderableBindings = {
		Binding{ "ObjectUBO", DescriptorSetLayout::Kind::UniformBuffer, 3, 2 },
	};

	/** Per-material buffer slots begin after EngineUBO/ReflectionUBO/ObjectUBO; texture units
	 * use their own namespace and start at 0. */
	inline constexpr uint32_t kFirstMaterialBufferSlot = 3;

	constexpr uint32_t SlotOf(uint32_t p_set, uint32_t p_binding)
	{
		if (p_set == kPerViewDescriptorSet)
		{
			for (const Binding& binding : kPerViewBindings)
			{
				if (binding.binding == p_binding) { return binding.slot; }
			}
		}
		else if (p_set == kPerRenderableDescriptorSet)
		{
			for (const Binding& binding : kPerRenderableBindings)
			{
				if (binding.binding == p_binding) { return binding.slot; }
			}
		}
		return ~uint32_t{ 0 };
	}
}
