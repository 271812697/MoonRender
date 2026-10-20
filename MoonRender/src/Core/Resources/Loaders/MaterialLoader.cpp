#include <tinyxml2.h>
#include <Core/Resources/Loaders/MaterialLoader.h>
#include <fstream>
#include <iostream>
#include <iterator>

namespace
{
	/** Old .ovmat files still point at the pre-rename ".ovfx" shader files. The assets are
	 * migrated in place on load, so a user's saved material keeps working. */
	void MigrateLegacyShaderPath(const std::string& p_path)
	{
		std::ifstream in(p_path);
		if (!in.is_open())
		{
			return;
		}
		const std::string text{ std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
		in.close();

		if (text.find(".ovfx") == std::string::npos)
		{
			return;
		}

		std::string migrated = text;
		for (size_t at = migrated.find(".ovfx"); at != std::string::npos; at = migrated.find(".ovfx", at + 4))
		{
			migrated.replace(at, 5, ".mat");
		}

		std::ofstream out(p_path, std::ios::binary | std::ios::trunc);
		out << migrated;
		std::cout << "[MATERIAL] \"" << p_path << "\": migrated .ovfx to .mat" << std::endl;
	}
}


Core::Resources::Material * Core::Resources::Loaders::MaterialLoader::Create(const std::string & p_path)
{
	MigrateLegacyShaderPath(p_path);

	tinyxml2::XMLDocument doc;
	doc.LoadFile(p_path.c_str());
	if (!doc.Error())
	{
		tinyxml2::XMLNode* root = doc.FirstChild();

		Material* material = new Material();

		material->OnDeserialize(doc, root);

		return material;
	}
	else
	{
		return nullptr;
	}
}

void Core::Resources::Loaders::MaterialLoader::Reload(Material& p_material, const std::string& p_path)
{
	MigrateLegacyShaderPath(p_path);

	tinyxml2::XMLDocument doc;
	doc.LoadFile(p_path.c_str());
	if (!doc.Error())
	{
		tinyxml2::XMLNode* root = doc.FirstChild();
		p_material.OnDeserialize(doc, root);
		std::cout << ("[MATERIAL] \"" + p_path + "\" Reloaded") << std::endl;
	}
}

void Core::Resources::Loaders::MaterialLoader::Save(Material& p_material, const std::string& p_path)
{
	tinyxml2::XMLDocument doc;
	tinyxml2::XMLNode* node = doc.NewElement("root");
	doc.InsertFirstChild(node);

	p_material.OnSerialize(doc, node);

	if (doc.SaveFile(p_path.c_str()) == tinyxml2::XML_SUCCESS)
		std::cout << ("[MATERIAL] \"" + p_path + "\": Saved") << std::endl;
	else
	{
		std::cout << ("[MATERIAL] \"" + p_path + "\": Failed to save") << std::endl;;
	}
		
}

bool Core::Resources::Loaders::MaterialLoader::Destroy(Material *& p_material)
{
	if (p_material)
	{
		delete p_material;
		p_material = nullptr;

		return true;
	}

	return false;
}