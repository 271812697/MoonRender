#pragma once
#include <memory>
#include <string>

#include <Core/ResourceManagement/MaterialManager.h>
#include <Core/ResourceManagement/ModelManager.h>
#include <Core/ResourceManagement/ShaderManager.h>
#include <Core/ResourceManagement/TextureManager.h>
#include <Core/SceneSystem/SceneManager.h>
#include <Rendering/Context/Driver.h>

namespace Core
{
	/** The engine: the driver and the resource managers, all of them bound to one GL context.
	 *
	 * It is built on that context (the driver allocates its backend there, the managers create
	 * their defaults) and it has to be destroyed on it - which is what the host that owns it
	 * takes care of (see Core::RenderModuleHost).
	 *
	 * Nothing here knows about Qt or about the editor: the context comes from whoever implements
	 * Rendering::Context::IGLContextHost, and the editor reaches this through the alias in
	 * Moon/renderer/Context.h. */
	class Context
	{
	public:
		/** Creates the driver itself and initializes it on the context that is current here (the
		 * single-threaded path). */
		Context(const std::string& p_projectPath, const std::string& p_projectName);
		/** Takes a driver that the platform already created on the module's thread - the shape
		 * Filament uses: the platform (Qt, for us) creates the driver inside the engine's render
		 * thread, and the engine only adopts it. */
		explicit Context(std::unique_ptr<::Rendering::Context::Driver> p_driver);
		virtual ~Context();

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
