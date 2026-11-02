#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "MaterialBuilder.h"

/** The uniform half of the PerMaterial descriptor layout (filament's UibGenerator / the
 * BufferInterfaceBlock): every non-sampler parameter of the material block becomes a std140
 * field with an offset and a stride, and the block's total size.
 *
 * UibGenerator.cpp owns the offsets; UboBlockInfo is what the package stores and what the
 * runtime uses to write a MaterialInstance's parameter block by name. */
namespace Moon::Matc
{
	struct UniformInfo
	{
		std::string name;
		ParameterType type = ParameterType::Unknown;
		uint32_t offset = 0;   // std140 offset inside the block
		uint32_t stride = 0;   // element size (arrays: the padded stride)
		uint32_t count = 1;    // array length, 1 for a plain field
	};

	struct UboBlockInfo
	{
		std::string name = "UboMaterial";
		uint32_t size = 0;     // the block's std140 size, what the runtime allocates
		std::vector<UniformInfo> uniforms;
	};

	/** The std140 size and base alignment of a parameter type. Samplers never reach this:
	 * they are not allowed inside a uniform block and stay separate uniforms. */
	struct TypeLayout
	{
		uint32_t size;
		uint32_t alignment;
	};

	TypeLayout ParameterTypeLayout(ParameterType p_type);

	/** Assigns the std140 offsets/strides and the sampler units, and writes them back into the
	 * builder's parameters (the package reflects those); BuildUniformBlock reads them back. */
	void LayoutParameters(MaterialBuilder& p_material);

	/** Adds the engine-owned globals (camera, lights, shadow matrix) to the parameter table. */
	void InjectEngineGlobals(MaterialBuilder& p_material);

	/** The GLSL `layout(std140) uniform UboMaterial { ... }` text for a laid-out material. */
	std::string ParametersPrelude(const std::vector<Parameter>& p_parameters, uint32_t p_blockSize);

	/** The uniform block of a laid-out material. */
	UboBlockInfo BuildUniformBlock(const MaterialBuilder& p_material);
}
