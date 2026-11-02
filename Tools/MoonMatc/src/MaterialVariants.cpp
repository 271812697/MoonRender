// The variant enumeration: see MaterialVariants.h. Split out of Matc.cpp's CompileSource.

#include "MaterialVariants.h"

namespace Moon::Matc
{
	namespace
	{
		/** The default pass (empty name) first, then the material's extra passes. */
		std::vector<std::string> PassesOf(const MaterialBuilder& p_material)
		{
			std::vector<std::string> passes;
			passes.emplace_back();
			for (const std::string& pass : p_material.passes) { passes.push_back(pass); }
			return passes;
		}

		/** One entry per pass, per engine-variant mask (or a single entry when the material
		 * enumerates no engine variants). */
		void AppendVariants(
			std::vector<PassVariant>& p_out,
			const std::vector<std::string>& p_passes,
			const std::vector<std::string>& p_variantDefines,
			const std::vector<std::string>& p_baseFeatures)
		{
			for (const std::string& pass : p_passes)
			{
				if (p_variantDefines.empty())
				{
					PassVariant variant;
					variant.pass = pass;
					for (const std::string& feature : p_baseFeatures) { variant.features.insert(feature); }
					p_out.push_back(std::move(variant));
					continue;
				}

				for (size_t mask = 0; mask < (size_t{ 1 } << p_variantDefines.size()); ++mask)
				{
					PassVariant variant;
					variant.pass = pass;
					for (const std::string& feature : p_baseFeatures) { variant.features.insert(feature); }
					for (size_t bit = 0; bit < p_variantDefines.size(); ++bit)
					{
						if (mask & (size_t{ 1 } << bit)) { variant.features.insert(p_variantDefines[bit]); }
					}
					p_out.push_back(std::move(variant));
				}
			}
		}
	}

	std::string VariantDefine(const std::string& p_name)
	{
		return p_name == "DEPTH" ? std::string("VARIANT_DEPTH") : ("VARIANT_HAS_" + p_name);
	}

	std::vector<PassVariant> EnumerateVariants(const MaterialBuilder& p_material, const CompileOptions& p_options)
	{
		std::vector<PassVariant> variants;
		const std::vector<std::string> passes = PassesOf(p_material);

		std::vector<std::string> variantDefines;
		for (const std::string& name : p_material.variantFilter)
		{
			variantDefines.push_back(VariantDefine(name));
		}

		if (!p_options.enabledFeatures.empty())
		{
			// The designer chose a feature set: compile exactly that set, plus the engine
			// variants when the material enumerates them.
			AppendVariants(variants, passes, variantDefines, p_options.enabledFeatures);
		}
		else if (!p_material.variantFilter.empty())
		{
			// The archive build: the material's fixed default features plus the engine variants.
			// A material with optional features declares defaultFeatures (possibly empty).
			AppendVariants(variants, passes, variantDefines,
				p_material.hasDefaultFeatures ? p_material.defaultFeatures : std::vector<std::string>{});
		}
		else if (p_material.hasDefaultFeatures)
		{
			// The archive build with no engine variants: the default feature set is exactly what
			// the archive ships, the same way the filter path treats it. Only the materials
			// without a defaultFeatures declaration (the ones that rely on per-instance
			// features) fall through to the power set below - the lit materials used to land
			// there once their variantFilter was dropped, which multiplied Standard's five
			// optional features into 96 variants.
			AppendVariants(variants, passes, variantDefines, p_material.defaultFeatures);
		}
		else
		{
			const size_t featureCount = p_material.features.size();
			for (const std::string& pass : passes)
			{
				for (size_t mask = 0; mask < (size_t{ 1 } << featureCount); ++mask)
				{
					PassVariant variant;
					variant.pass = pass;
					for (size_t bit = 0; bit < featureCount; ++bit)
					{
						if (mask & (size_t{ 1 } << bit)) { variant.features.insert(p_material.features[bit]); }
					}
					variants.push_back(std::move(variant));
				}
			}
		}

		return variants;
	}
}
