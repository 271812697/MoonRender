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

		/** Whether a service of that type has been provided.
		 *
		 * Something that runs before the part of the application a service belongs to
		 * is up - a poll that watches a renderer pass, a slot that may be reached
		 * during start-up - asks this first: Get() on a service that was never
		 * provided throws, and the message it throws says nothing about which one.
		 * (Careful: Get() itself inserts an empty entry, so this has to be asked
		 * before it.) */
		template<typename T>
		static bool IsProvided()
		{
			const auto found = __SERVICES.find(typeid(T).hash_code());
			return found != __SERVICES.end() && found->second.has_value();
		}

	private:
		static std::unordered_map<size_t, std::any> __SERVICES;
	};
}
