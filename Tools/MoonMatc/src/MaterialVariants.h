#pragma once
#include <string>
#include <vector>

#include "Matc.h"
#include "MaterialBuilder.h"

/** Which (pass, feature-set) pairs this build compiles - filament's MaterialVariants.
 *
 * A material that declares variantFilter enumerates the engine variant bits (the Filament
 * model); the archive otherwise compiles the fixed default feature set, or - for the materials
 * that still rely on per-instance features - the feature power set. */
namespace Moon::Matc
{
	/** "DIRECTIONAL_LIGHTING" -> "VARIANT_HAS_DIRECTIONAL_LIGHTING" (DEPTH -> VARIANT_DEPTH). */
	std::string VariantDefine(const std::string& p_name);

	/** Every variant this build compiles, in the order the package lists them. */
	std::vector<PassVariant> EnumerateVariants(const MaterialBuilder& p_material, const CompileOptions& p_options);
}
