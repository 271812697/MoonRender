#include "Core/ResourceManagement/ShaderManager.h"

Rendering::Data::Material* Core::ResourceManagement::ShaderManager::CreateResource(const std::string& p_path)
{
	std::string realPath = GetRealPath(p_path);
	auto pathParserCallback = std::bind(&Core::ResourceManagement::ShaderManager::GetRealPath, this, std::placeholders::_1);
	Rendering::Data::Material* material = Rendering::Resources::Loaders::ShaderLoader::Create(realPath, pathParserCallback);
	if (material)
	{
		material->path = p_path;
	}
	return material;
}

Rendering::Data::Material* Core::ResourceManagement::ShaderManager::CreateFromSource(const std::string& p_source)
{
	auto pathParserCallback = std::bind(&Core::ResourceManagement::ShaderManager::GetRealPath, this, std::placeholders::_1);
	return Rendering::Resources::Loaders::ShaderLoader::CreateFromSource(p_source, pathParserCallback);
}

void Core::ResourceManagement::ShaderManager::DestroyResource(Rendering::Data::Material* p_resource)
{
	Rendering::Resources::Loaders::ShaderLoader::Destroy(p_resource);
}

void Core::ResourceManagement::ShaderManager::ReloadResource(Rendering::Data::Material* p_resource, const std::string& p_path)
{
	auto pathParserCallback = std::bind(&Core::ResourceManagement::ShaderManager::GetRealPath, this, std::placeholders::_1);
	Rendering::Resources::Loaders::ShaderLoader::Recompile(*p_resource, p_path, pathParserCallback);
}
