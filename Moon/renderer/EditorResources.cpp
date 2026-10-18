#include <filesystem>
#include <Core/Helpers/GUIDrawer.h>
#include "EditorResources.h"
#include <Rendering/Settings/ETextureFilteringMode.h>
#include <Tools/Utils/PathParser.h>
#include <assert.h>

namespace
{
	template<Rendering::Settings::ETextureFilteringMode FilteringMode>
	auto CreateTexture(const std::filesystem::path& p_path)
	{
		return Rendering::Resources::Loaders::TextureLoader::Create(
			p_path.string(),
			FilteringMode,
			FilteringMode,
			Rendering::Settings::ETextureWrapMode::REPEAT,
			Rendering::Settings::ETextureWrapMode::REPEAT,
			false
		);
	}

	auto CreateModel(const std::filesystem::path& p_path)
	{
		const auto modelParserFlags =
			Rendering::Resources::Parsers::EModelParserFlags::TRIANGULATE |
			Rendering::Resources::Parsers::EModelParserFlags::GEN_SMOOTH_NORMALS |
			Rendering::Resources::Parsers::EModelParserFlags::OPTIMIZE_MESHES |
			Rendering::Resources::Parsers::EModelParserFlags::FIND_INSTANCES |
			Rendering::Resources::Parsers::EModelParserFlags::CALC_TANGENT_SPACE |
			Rendering::Resources::Parsers::EModelParserFlags::JOIN_IDENTICAL_VERTICES |
			Rendering::Resources::Parsers::EModelParserFlags::DEBONE |
			Rendering::Resources::Parsers::EModelParserFlags::FIND_INVALID_DATA |
			Rendering::Resources::Parsers::EModelParserFlags::IMPROVE_CACHE_LOCALITY |
			Rendering::Resources::Parsers::EModelParserFlags::GEN_UV_COORDS |
			Rendering::Resources::Parsers::EModelParserFlags::PRE_TRANSFORM_VERTICES |
			Rendering::Resources::Parsers::EModelParserFlags::GLOBAL_SCALE;

		return Rendering::Resources::Loaders::ModelLoader::Create(
			p_path.string(),
			modelParserFlags
		);
	}

	template<typename T>
	auto ValidateResources(const std::unordered_map<std::string, T>& p_resources)
	{
		for (const auto& [id, resource] : p_resources)
		{
			assert(resource != nullptr&&"Failed to load resource with ID: ");
		}
	}

	template<typename T>
	T TryGetResource(std::unordered_map<std::string, T>& p_resources, const std::string& p_id)
	{
		return
			p_resources.find(p_id) != p_resources.end() ?
			p_resources.at(p_id) :
			nullptr;
	}
}

Editor::Core::EditorResources::EditorResources(const std::string& p_editorAssetsPath)
{
	using namespace Rendering::Resources::Loaders;
	using enum Rendering::Settings::ETextureFilteringMode;

	const auto editorAssetsPath = std::filesystem::path{ p_editorAssetsPath };
	const auto texturesFolder = editorAssetsPath / "Textures";
	const auto modelsFolder = editorAssetsPath / "Models";

	m_textures = {
		{"Play", CreateTexture<LINEAR>(texturesFolder / "Play.png")},
		{"Pause", CreateTexture<LINEAR>(texturesFolder / "Pause.png")},
		{"Stop", CreateTexture<LINEAR>(texturesFolder / "Stop.png")},
		{"Next", CreateTexture<LINEAR>(texturesFolder / "Next.png")},
		{"Refresh", CreateTexture<LINEAR>(texturesFolder / "Refresh.png")},
		{"Me", CreateTexture<LINEAR>(texturesFolder / "Move.png")},
		{"Rotate", CreateTexture<LINEAR>(texturesFolder / "Rotate.png")},
		{"Scale", CreateTexture<LINEAR>(texturesFolder / "Scale.png")},
		{"File", CreateTexture<LINEAR>(texturesFolder / "File.png")},
		{"Folder", CreateTexture<LINEAR>(texturesFolder / "Folder.png")},
		{"Texture", CreateTexture<LINEAR>(texturesFolder / "Texture.png")},
		{"Model", CreateTexture<LINEAR>(texturesFolder / "Model.png")},
		{"Shader", CreateTexture<LINEAR>(texturesFolder / "Shader.png")},
		{"Shader_Part", CreateTexture<LINEAR>(texturesFolder / "Shader_Part.png")},
		{"Material", CreateTexture<LINEAR>(texturesFolder / "Material.png")},
		{"Scene", CreateTexture<LINEAR>(texturesFolder / "Scene.png")},
		{"Sound", CreateTexture<LINEAR>(texturesFolder / "Sound.png")},
		{"Script", CreateTexture<LINEAR>(texturesFolder / "Script.png")},
		{"Font", CreateTexture<LINEAR>(texturesFolder / "Font.png")},
		{"Point_Light", CreateTexture<NEAREST>(texturesFolder / "Point_Light.png")},
		{"Spot_Light", CreateTexture<NEAREST>(texturesFolder / "Spot_Light.png")},
		{"Directional_Light", CreateTexture<NEAREST>(texturesFolder / "Directional_Light.png")},
		{"Ambient_Box_Light", CreateTexture<NEAREST>(texturesFolder / "Ambient_Box_Light.png")},
		{"Ambient_Sphere_Light", CreateTexture<NEAREST>(texturesFolder / "Ambient_Sphere_Light.png")},
		{"Empty_Texture", CreateTexture<LINEAR>(texturesFolder / "Empty_Texture.png")}
	};

	m_models = {
		{"Cube", CreateModel(modelsFolder / "Cube.fbx")},
		{"Cylinder", CreateModel(modelsFolder / "Cylinder.fbx")},
		{"Plane", CreateModel(modelsFolder / "Plane.fbx")},
		{"Vertical_Plane", CreateModel(modelsFolder / "Vertical_Plane.fbx")},
		{"Roll", CreateModel(modelsFolder / "Roll.fbx")},
		{"Sphere", CreateModel(modelsFolder / "Sphere.fbx")},
		{"Arrow_Translate", CreateModel(modelsFolder / "Arrow_Translate.fbx")},
		{"Arrow_Rotate", CreateModel(modelsFolder / "Arrow_Rotate.fbx")},
		{"Arrow_Scale", CreateModel(modelsFolder / "Arrow_Scale.fbx")},
		{"Arrow_Picking", CreateModel(modelsFolder / "Arrow_Picking.fbx")},
		{"Camera", CreateModel(modelsFolder / "Camera.fbx")}
	};

	// Ensure that all resources have been loaded successfully
	ValidateResources(m_textures);
	ValidateResources(m_models);

	// Register the empty texture for the GUIDrawer to use it when a texture is missing
	::Core::Helpers::GUIDrawer::ProvideEmptyTexture(*m_textures["Empty_Texture"]);
}

Editor::Core::EditorResources::~EditorResources()
{
	for (auto& [_, texture] : m_textures)
	{
		Rendering::Resources::Loaders::TextureLoader::Destroy(texture);
	}

	for (auto& [_, mesh] : m_models)
	{
		Rendering::Resources::Loaders::ModelLoader::Destroy(mesh);
	}
}

Rendering::Resources::Texture* Editor::Core::EditorResources::GetFileIcon(const std::string& p_filename)
{
	using namespace Tools::Utils;

	const PathParser::EFileType fileType = PathParser::GetFileType(p_filename);

	return GetTexture(
		fileType == PathParser::EFileType::UNKNOWN ?
		"File" : // If the file type is unknown, we return the "File" icon
		PathParser::FileTypeToString(fileType) // Otherwise we return the icon corresponding to the file type
	);
}

Rendering::Resources::Texture* Editor::Core::EditorResources::GetTexture(const std::string& p_id)
{
	return TryGetResource(m_textures, p_id);
}

Rendering::Resources::Model* Editor::Core::EditorResources::GetModel(const std::string& p_id)
{
	return TryGetResource(m_models, p_id);
}
