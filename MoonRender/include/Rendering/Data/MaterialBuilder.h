#pragma once
#include <memory>
#include <string>

#include <Rendering/Data/Material.h>
#include <Rendering/Data/MaterialDefinition.h>

namespace Rendering::Data
{
	/** The runtime counterpart of filamat's MaterialBuilder: a package (via MaterialParser)
	 * becomes the immutable MaterialDefinition; the definition plus its compiled variants
	 * becomes a Material (the variant-program cache). */
	class MaterialBuilder
	{
	public:
		static std::shared_ptr<const MaterialDefinition> Build(const Material& p_material);
		static std::unique_ptr<Material> BuildMaterial(
			std::string p_path,
			std::shared_ptr<const MaterialDefinition> p_definition,
			Material::Variants p_variants);
	};
}
