// The sampler side of the PerMaterial layout: see SibGenerator.h. Split out of Matc.cpp /
// ShaderGenerator.cpp - the sampler predicates and the block they describe live in
// ParameterTypes.h / this file.

#include "SibGenerator.h"

#include <Rendering/Data/EngineDescriptorSets.h>

namespace Moon::Matc
{
	SamplerBlockInfo CollectSamplerBlock(const MaterialBuilder& p_material)
	{
		SamplerBlockInfo block;
		for (const Parameter& parameter : p_material.parameters)
		{
			if (!IsSamplerType(parameter.type)) { continue; }
			// View-owned IBL textures belong to the per-view set; they are not material
			// descriptors and must not be serialized into MAT_SIB.
			if (Rendering::Data::EngineDescriptorSets::IsGlobalSampler(parameter.name)) { continue; }

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
