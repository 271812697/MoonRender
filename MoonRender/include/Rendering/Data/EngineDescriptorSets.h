#pragma once
#include <array>
#include <cstdint>
#include <string_view>

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
		/** GLSL file that declares this block (UBOs only). */
		const char* include = nullptr;
		/** GLSL sampler type (textures only). */
		const char* glslType = nullptr;
	};

	inline constexpr std::array<Binding, 5> kPerViewBindings = {
		Binding{ "EngineUBO", DescriptorSetLayout::Kind::UniformBuffer, 0, 0,
			"Common/Buffers/EngineUBO.glsl" },
		Binding{ "ReflectionUBO", DescriptorSetLayout::Kind::UniformBuffer, 1, 1,
			"Common/Buffers/ReflectionUBO.glsl" },
		// View-owned IBL textures. They are not material parameters: every lit material reads
		// the same LUT and environment cubes for the current view.
		Binding{ "_BRDFLut", DescriptorSetLayout::Kind::Texture, 2, 0, nullptr, "sampler2D" },
		Binding{ "_IrradianceCube", DescriptorSetLayout::Kind::Texture, 3, 1, nullptr, "samplerCube" },
		Binding{ "_PrefilterCube", DescriptorSetLayout::Kind::Texture, 4, 2, nullptr, "samplerCube" },
	};

	inline constexpr std::array<Binding, 1> kPerRenderableBindings = {
		Binding{ "ObjectUBO", DescriptorSetLayout::Kind::UniformBuffer, 3, 2,
			"Common/Buffers/ObjectUBO.glsl" },
	};

	inline constexpr uint32_t kBrdfLutBinding = 2;
	inline constexpr uint32_t kIrradianceCubeBinding = 3;
	inline constexpr uint32_t kPrefilterCubeBinding = 4;

	/** Per-material buffer slots begin after EngineUBO/ReflectionUBO/ObjectUBO; its texture
	 * units begin after the three fixed per-view textures. */
	inline constexpr uint32_t kFirstMaterialBufferSlot = 3;
	inline constexpr uint32_t kFirstMaterialTextureSlot = 3;

	constexpr bool IsGlobalSampler(std::string_view p_name)
	{
		return p_name == "_BRDFLut"
			|| p_name == "_IrradianceCube"
			|| p_name == "_PrefilterCube";
	}

	constexpr uint32_t GlobalSamplerUnit(std::string_view p_name)
	{
		return p_name == "_BRDFLut" ? 0
			: p_name == "_IrradianceCube" ? 1
			: p_name == "_PrefilterCube" ? 2
			: ~uint32_t{ 0 };
	}

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
