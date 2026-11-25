#include <Rendering/Resources/Texture.h>

#include <Rendering/Settings/ETextureType.h>

Rendering::Resources::Texture::Texture()
	: m_texture(Rendering::Settings::ETextureType::TEXTURE_2D)
{
}

Rendering::Resources::Texture::Texture(const std::string p_path)
	: path(p_path)
	, m_texture(Rendering::Settings::ETextureType::TEXTURE_2D)
{
}

Rendering::Resources::Texture::Texture(const std::string p_path, std::string_view p_debugName)
	: path(p_path)
	, m_texture(Rendering::Settings::ETextureType::TEXTURE_2D, p_debugName)
{
}

Rendering::Resources::Texture::Texture(Settings::ETextureType p_type, std::string_view p_debugName)
	: m_texture(p_type, p_debugName)
{
}

Rendering::Resources::Texture::~Texture() = default;
