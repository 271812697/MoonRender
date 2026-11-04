#pragma once
#include <Rendering/Resources/Loaders/ShaderLoader.h>
#include "Core/ResourceManagement/AResourceManager.h"

namespace Core::ResourceManagement
{
	/** Manages package materials. The historical name is kept for the service locator; a
	 * package is now a Material, not a separate Shader class. */
	class ShaderManager : public AResourceManager<::Rendering::Data::Material>
	{
	public:
		virtual ::Rendering::Data::Material* CreateResource(const std::string& p_path) override;
		::Rendering::Data::Material* CreateFromSource(const std::string& p_source);
		virtual void DestroyResource(::Rendering::Data::Material* p_resource) override;
		virtual void ReloadResource(::Rendering::Data::Material* p_resource, const std::string& p_path) override;
	};
}
