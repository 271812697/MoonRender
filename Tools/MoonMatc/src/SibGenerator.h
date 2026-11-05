#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "MaterialBuilder.h"

/** The sampler half of the PerMaterial descriptor layout (filament's SibGenerator / the
 * SamplerInterfaceBlock). Every sampler parameter of the material block becomes one descriptor:
 * a texture unit, the sampler type, and which 1x1 texture the runtime binds when the instance
 * assigned none.
 *
 * SibGenerator.cpp owns the assignments; SamplerBlockInfo is what the package stores (and what
 * the runtime's MaterialInstance looks a texture up by). */
namespace Moon::Matc
{
	struct SamplerInfo
	{
		std::string name;
		ParameterType type = ParameterType::Unknown;
		uint32_t binding = 0;  // the texture unit
		SamplerFallback fallback = SamplerFallback::Default;
	};

	struct SamplerBlockInfo
	{
		std::string name = "PerMaterial";
		std::vector<SamplerInfo> samplers;
	};

	/** The sampler block of a laid-out material (units assigned by LayoutParameters). */
	SamplerBlockInfo CollectSamplerBlock(const MaterialBuilder& p_material);
}
