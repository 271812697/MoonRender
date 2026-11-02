#include <Rendering/Data/MaterialBuilder.h>

#include <Rendering/Data/MaterialDefinition.h>
#include <Rendering/Resources/Shader.h>
#include <Rendering/Resources/ShaderPackage.h>

namespace Rendering::Data
{
	std::shared_ptr<const MaterialDefinition> MaterialBuilder::Build(
		const Rendering::Resources::ShaderPackage& p_package)
	{
		return MaterialDefinition::FromPackage(p_package);
	}

	std::shared_ptr<const MaterialDefinition> MaterialBuilder::Build(
		const Rendering::Resources::Shader& p_shader)
	{
		return MaterialDefinition::FromShader(p_shader);
	}
}
