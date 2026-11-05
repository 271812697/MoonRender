#include "Core/ResourceManagement/MaterialManager.h"

Core::Resources::MaterialAsset * Core::ResourceManagement::MaterialManager::CreateResource(const std::string & p_path)
{
	std::string realPath = GetRealPath(p_path);

	Core::Resources::MaterialAsset* material = Core::Resources::Loaders::MaterialAssetLoader::Create(realPath);
	if (material)
	{
		material->path = p_path;
	}

	return material;
}

void Core::ResourceManagement::MaterialManager::DestroyResource(Core::Resources::MaterialAsset * p_resource)
{
	Core::Resources::Loaders::MaterialAssetLoader::Destroy(p_resource);
}

void Core::ResourceManagement::MaterialManager::ReloadResource(Core::Resources::MaterialAsset* p_resource, const std::string& p_path)
{
	std::string realPath = GetRealPath(p_path);
	Core::Resources::Loaders::MaterialAssetLoader::Reload(*p_resource, realPath);
}
