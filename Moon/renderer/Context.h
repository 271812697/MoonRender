#pragma once

#include <Core/ResourceManagement/MaterialManager.h>
#include <Core/ResourceManagement/ModelManager.h>
#include <Core/ResourceManagement/ShaderManager.h>
#include <Core/ResourceManagement/TextureManager.h>
#include <Core/SceneSystem/SceneManager.h>

namespace Editor::Core
{
	class Context
	{
	public:
		Context(const std::string& p_projectPath, const std::string& p_projectName);
		virtual ~Context();
	public:
		std::string engineAssetsPath;
		std::string editorAssetsPath;
		std::unique_ptr<::Rendering::Context::Driver> driver;
		::Core::SceneSystem::SceneManager sceneManager;
		::Core::ResourceManagement::ModelManager modelManager;
		::Core::ResourceManagement::TextureManager textureManager;
		::Core::ResourceManagement::ShaderManager shaderManager;
		::Core::ResourceManagement::MaterialManager materialManager;
	};
}
