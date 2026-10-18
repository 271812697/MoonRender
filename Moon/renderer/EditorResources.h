#pragma once
#include <Core/ResourceManagement/TextureManager.h>
#include <Core/ResourceManagement/ModelManager.h>
#include <Core/ResourceManagement/ShaderManager.h>

namespace Editor::Core
{
	/**
	* Handle the creation and storage of editor specific resources
	*
	* Shaders used to live here too (five copies under Data/Editor/Shaders), but nothing ever
	* asked for them: every draw site loads its shader through ShaderManager. They are gone,
	* and with them the last reason the runtime needed a shader folder on disk.
	*/
	class EditorResources
	{
	public:
		/**
		* Constructor
		* @param p_editorAssetsPath
		*/
		EditorResources(const std::string& p_editorAssetsPath);

		/**
		* Destructor
		*/
		~EditorResources();

		/**
		* Returns the file icon identified by the given string or nullptr on fail
		* @param p_filename
		*/

		::Rendering::Resources::Texture* GetFileIcon(const std::string& p_filename);

		/**
		* Returns the texture identified by the given string or nullptr on fail
		* @param p_id
		*/
		::Rendering::Resources::Texture* GetTexture(const std::string& p_id);

		/**
		* Returns the model identified by the given string or nullptr on fail
		* @param p_id
		*/
		::Rendering::Resources::Model* GetModel(const std::string& p_id);

	private:
		std::unordered_map<std::string, ::Rendering::Resources::Texture*> m_textures;
		std::unordered_map<std::string, ::Rendering::Resources::Model*> m_models;
	};
}