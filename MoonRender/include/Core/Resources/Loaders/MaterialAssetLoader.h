#pragma once
#include "Core/Resources/MaterialAsset.h"
namespace Core::Resources::Loaders
{
	/**
	* Handle the creation of materials
	*/
	class MaterialAssetLoader
	{
	public:
		/**
		* Disabled constructor
		*/
		MaterialAssetLoader() = delete;

		/**
		* Instantiate a material from a file
		* @param p_path
		*/
		static MaterialAsset* Create(const std::string & p_path);

		/**
		* Reload the material using the given file path
		* @param p_material
		* @param p_path
		*/
		static void Reload(MaterialAsset& p_material, const std::string& p_path);

		/**
		* Save the material to the given path
		* @param p_material
		* @param p_path
		*/
		static void Save(MaterialAsset& p_material, const std::string& p_path);

		/**
		* Destroy the given material
		* @param p_material
		*/
		static bool Destroy(MaterialAsset*& p_material);
	};
}