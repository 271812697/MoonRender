#include <Core/Global/ServiceLocator.h>
#include "Context.h"
#include <Tools/Utils/PathParser.h>
using namespace Core::Global;
using namespace ::Core::ResourceManagement;


Editor::Core::Context::Context(const std::string& p_projectPath, const std::string& p_projectName)

{
	engineAssetsPath = Tools::Utils::PathParser::GetExeDirectory() + "/Moon/Data/Engine/";
	ModelManager::ProvideAssetPaths("", engineAssetsPath);
	TextureManager::ProvideAssetPaths("", engineAssetsPath);
	ShaderManager::ProvideAssetPaths("", engineAssetsPath);
	MaterialManager::ProvideAssetPaths("", engineAssetsPath);
	driver = std::make_unique<::Rendering::Context::Driver>(::Rendering::Settings::DriverSettings{ true });
	ServiceLocator::Provide<ModelManager>(modelManager);
	ServiceLocator::Provide<TextureManager>(textureManager);
	ServiceLocator::Provide<ShaderManager>(shaderManager);
	ServiceLocator::Provide<MaterialManager>(materialManager);
	ServiceLocator::Provide<::Core::SceneSystem::SceneManager>(sceneManager);
	ServiceLocator::Provide<Editor::Core::Context>(*this);
}

Editor::Core::Context::~Context()
{
	modelManager.UnloadResources();
	textureManager.UnloadResources();
	shaderManager.UnloadResources();
	materialManager.UnloadResources();

}
