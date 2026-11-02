#pragma once
#include <memory>

namespace Rendering::Resources
{
	class Shader;
	class ShaderPackage;
}

namespace Rendering::Data
{
	class MaterialDefinition;

	/** The runtime counterpart of filamat's MaterialBuilder: it turns a package (or a shader
	 * built from one) into the immutable MaterialDefinition the runtime draws with.
	 *
	 * All the package interpretation lives in MaterialDefinition; this class is the seam the
	 * loader uses, so a future runtime material (a second default instance, a different state
	 * mapping) has one place to go. */
	class MaterialBuilder
	{
	public:
		static std::shared_ptr<const MaterialDefinition> Build(
			const Rendering::Resources::ShaderPackage& p_package);
		static std::shared_ptr<const MaterialDefinition> Build(
			const Rendering::Resources::Shader& p_shader);
	};
}
