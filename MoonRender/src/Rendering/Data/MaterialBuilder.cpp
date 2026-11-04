#include <Rendering/Data/MaterialBuilder.h>

#include <Rendering/Data/Material.h>
#include <Rendering/Data/MaterialDefinition.h>

namespace Rendering::Data
{
	std::shared_ptr<const MaterialDefinition> MaterialBuilder::Build(
		const Material& p_material)
	{
		return MaterialDefinition::FromMaterial(p_material);
	}

	std::unique_ptr<Material> MaterialBuilder::BuildMaterial(
		std::string p_path,
		std::shared_ptr<const MaterialDefinition> p_definition,
		Material::Variants p_variants)
	{
		return std::make_unique<Material>(
			std::move(p_path), std::move(p_definition), std::move(p_variants));
	}
}
