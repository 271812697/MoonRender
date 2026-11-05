#pragma once
#include "Core/Resources/Loaders/MaterialAssetLoader.h"
#include "Core/ResourceManagement/AResourceManager.h"

namespace Core::ResourceManagement
{
	/**
	* ResourceManager of materials
	*/
	class MaterialManager : public AResourceManager<Core::Resources::MaterialAsset>
	{
	public:
		/**
		* Create the resource identified by the given path
		* @param p_path
		*/
		virtual Core::Resources::MaterialAsset* CreateResource(const std::string & p_path) override;

		/**
		* Destroy the given resource
		* @param p_resource
		*/
		virtual void DestroyResource(Core::Resources::MaterialAsset* p_resource) override;

		/**
		* Reload the given resource
		* @param p_resource
		* @param p_path
		*/
		virtual void ReloadResource(Core::Resources::MaterialAsset* p_resource, const std::string& p_path) override;
	};
}