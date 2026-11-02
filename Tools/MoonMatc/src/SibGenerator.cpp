// The sampler side of the PerMaterial layout: see SibGenerator.h. Split out of Matc.cpp /
// ShaderGenerator.cpp - the sampler predicates and the block they describe live here.

#include "SibGenerator.h"

namespace Moon::Matc
{
	/** Whether a parameter type is one of the sampler types: the slot a texture goes into,
	 * as opposed to a plain uniform. */
	bool IsSamplerType(const std::string& p_dslType)
	{
		return p_dslType.starts_with("sampler")
			|| p_dslType.starts_with("isampler")
			|| p_dslType.starts_with("usampler");
	}

	/** The 1x1 fallback a sampler gets when the designer assigned no map. The enum itself is in
	 * SibGenerator.h - the package and the runtime read it; this file only maps the declared
	 * name to it. */
	uint8_t SamplerFallbackKind(const std::string& p_name)
	{
		if (p_name == "flatNormal") { return static_cast<uint8_t>(SamplerFallback::FlatNormal); }
		return static_cast<uint8_t>(SamplerFallback::Default);
	}

	SamplerBlockInfo CollectSamplerBlock(const MaterialBuilder& p_material)
	{
		SamplerBlockInfo block;
		for (const Parameter& parameter : p_material.parameters)
		{
			if (!IsSamplerType(parameter.type)) { continue; }

			SamplerInfo sampler;
			sampler.name = parameter.name;
			sampler.type = parameter.type;
			sampler.binding = parameter.binding;
			sampler.fallback = SamplerFallbackKind(parameter.fallback);
			block.samplers.push_back(std::move(sampler));
		}
		return block;
	}
}
