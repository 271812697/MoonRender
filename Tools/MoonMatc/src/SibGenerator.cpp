// The sampler side of the PerMaterial layout: see SibGenerator.h. Split out of Matc.cpp /
// ShaderGenerator.cpp - the sampler predicates and the block they describe live in
// ParameterTypes.h / this file.

#include "SibGenerator.h"

namespace Moon::Matc
{
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
			sampler.fallback = parameter.fallback;
			block.samplers.push_back(std::move(sampler));
		}
		return block;
	}
}
