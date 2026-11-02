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
	/** The 1x1 textures the engine provides for an unassigned sampler. */
	enum class SamplerFallback : uint8_t
	{
		/** sampler2D: (1,1,1,1) - the neutral multiplier for albedo / metal / roughness / AO.
		 * samplerCube: black, which is what the engine always used. */
		Default = 0,
		/** (0.5, 0.5, 1.0): a normal map decodes it to the neutral tangent-space normal
		 * (0, 0, 1), i.e. "no perturbation". The white default would decode to (1, 1, 1). */
		FlatNormal = 1,
	};

	struct SamplerInfo
	{
		std::string name;
		std::string type;      // the GLSL sampler type
		uint32_t binding = 0;  // the texture unit
		uint8_t fallback = 0;  // SamplerFallback, as the package stores it
	};

	struct SamplerBlockInfo
	{
		std::string name = "PerMaterial";
		std::vector<SamplerInfo> samplers;
	};

	/** True for the sampler types of the material DSL (sampler2D / samplerCube / ...). */
	bool IsSamplerType(const std::string& p_dslType);

	/** The fallback kind for a `fallback : ...` declaration (0 = default, 1 = flat normal). */
	uint8_t SamplerFallbackKind(const std::string& p_name);

	/** The sampler block of a laid-out material (units assigned by LayoutParameters). */
	SamplerBlockInfo CollectSamplerBlock(const MaterialBuilder& p_material);
}
