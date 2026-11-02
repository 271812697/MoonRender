#pragma once
#include <set>
#include <string>
#include <vector>

#include "MaterialBuilder.h"
#include "ShaderGenerator.h"

/** The reflection JSON (`--dump-json`) for one material: the same view the material panel and
 * the tools read, independent of the package binary. Kept out of CompileSource so the
 * compile flow stays readable and the JSON schema has one owner. */
namespace Moon::Matc
{
	std::string BuildMaterialJson(
		const MaterialBuilder& p_material,
		const std::vector<PassVariant>& p_variants,
		const std::vector<VertexAttribute>& p_attributes,
		const std::set<std::string>& p_writtenFields,
		const std::string& p_sourceName);
}
