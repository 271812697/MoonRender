#pragma once
#include <any>
#include <unordered_map>
#define GetTreeView GetService(::MOON::TreeViewPanel)
#define GetSceneView GetService(::Editor::Panels::SceneView)
#define GetMainScene GetService(::Editor::Panels::SceneView).GetScene()
#define GetViewerWidget GetService(::MOON::ViewerWidget)
#define GetService(Type)  ::Core::Global::ServiceLocator::Get<Type>()
#define RegService(Type,T) ::Core::Global::ServiceLocator::Provide<Type>(T)
#define GetShaderService GetService(::Core::ResourceManagement::ShaderManager)
#define GetModelService GetService(::Core::ResourceManagement::ModelManager)
namespace Core::Global
{
	/**
	* Provide a way to access to core services
	*/
	class ServiceLocator
	{
	public:
		/**
		* Register a service in the service locator
		* @param p_service
		*/
		template<typename T>
		static void Provide(T& p_service)
		{
			__SERVICES[typeid(T).hash_code()] = std::any(&p_service);
		}

		/**
		* Returns a service of the given type (Make sure that your provided the service before calling this method)
		*/
		template<typename T>
		static T& Get()
		{
			return *std::any_cast<T*>(__SERVICES[typeid(T).hash_code()]);
		}

	private:
		static std::unordered_map<size_t, std::any> __SERVICES;
	};
}