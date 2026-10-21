#include <glad/glad.h>
#include <Rendering/HAL/OpenGL/GLTexture.h>
#include <Rendering/HAL/OpenGL/GLTypes.h>
#include <assert.h>
#include <iostream>

Tools::Eventing::Event<::Rendering::HAL::GLTexture&> Rendering::HAL::GLTexture::CreationEvent;
Tools::Eventing::Event<::Rendering::HAL::GLTexture&> Rendering::HAL::GLTexture::DestructionEvent;

namespace
{
	uint32_t CalculateMipMapLevels(uint32_t p_width, uint32_t p_height)
	{
		uint32_t maxDim = p_width > p_height ? p_width : p_height;
		uint32_t t = __lzcnt(maxDim);
		return maxDim ? 32u -t : 1u;
	}

	constexpr bool IsValidMipMapFilter(Rendering::Settings::ETextureFilteringMode p_mode)
	{
		return
			p_mode == Rendering::Settings::ETextureFilteringMode::NEAREST_MIPMAP_NEAREST ||
			p_mode == Rendering::Settings::ETextureFilteringMode::NEAREST_MIPMAP_LINEAR ||
			p_mode == Rendering::Settings::ETextureFilteringMode::LINEAR_MIPMAP_NEAREST ||
			p_mode == Rendering::Settings::ETextureFilteringMode::LINEAR_MIPMAP_LINEAR;
	}
}

template<>
Rendering::HAL::GLTexture::TTexture(Settings::ETextureType p_type, std::string_view p_debugName) : GLTextureHandle(p_type)
{
	glCreateTextures(m_context.type, 1, &m_context.id);
	m_textureContext.debugName = p_debugName;
	CreationEvent.Invoke(*this);
}

template<>
Rendering::HAL::GLTexture::~TTexture()
{
	if (m_textureContext.desc.isTextureBuffer&& m_textureContext.desc.texBufferId!=0) {
		glDeleteBuffers(1, &m_textureContext.desc.texBufferId);
	}
	glDeleteTextures(1, &m_context.id);
	DestructionEvent.Invoke(*this);
}

template<>
void Rendering::HAL::GLTexture::Allocate(const Settings::TextureDesc& p_desc)
{
	auto& desc = m_textureContext.desc;
	
	if (!p_desc.isTextureBuffer) {
		desc = p_desc;
		desc.width = std::max(1u, desc.width);
		desc.height = std::max(1u, desc.height);

		if(desc.mutableDesc.has_value())
		{
			const auto& mutableDesc = desc.mutableDesc.value();

			assert(m_context.type != GL_TEXTURE_CUBE_MAP&& "Mutable textures are only supported for 2D textures");

			// No DSA version for glTexImage2D (mutable texture),
			// so we need to Bind/Unbind the texture.
			Bind();
			if (m_context.type == GL_TEXTURE_2D) {
				glTexImage2D(
					m_context.type,
					0,
					EnumToValue<GLenum>(desc.internalFormat),
					desc.width,
					desc.height,
					0,
					EnumToValue<GLenum>(mutableDesc.format),
					EnumToValue<GLenum>(mutableDesc.type),
					mutableDesc.data
				);
			}
			else if(m_context.type == GL_TEXTURE_2D_MULTISAMPLE)
			{
				glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, 4, EnumToValue<GLenum>(desc.internalFormat), desc.width, desc.height, GL_TRUE);

			}
			else if (m_context.type == GL_TEXTURE_2D_ARRAY)
			{
				glTexImage3D(
					m_context.type,
					0,
					EnumToValue<GLenum>(desc.internalFormat),
					desc.width,
					desc.height,
					mutableDesc.arrayLayers,
					0,
					EnumToValue<GLenum>(mutableDesc.format),
					EnumToValue<GLenum>(mutableDesc.type),
					mutableDesc.data
				);
			}

			Unbind();
		}
		else
		{
			// If the underlying texture is a cube map, this will allocate all 6 sides.
			// No need to iterate over each side.
			if (m_context.type != GL_TEXTURE_2D_MULTISAMPLE) {
				glTextureStorage2D(
					m_context.id,
					desc.mipLevels != 0
						? desc.mipLevels
						: (desc.useMipMaps ? CalculateMipMapLevels(desc.width, desc.height) : 1),
					EnumToValue<GLenum>(desc.internalFormat),
					desc.width,
					desc.height
				);
			}
			else
			{
				glTextureStorage2DMultisample(
					m_context.id, 
					4,
					EnumToValue<GLenum>(desc.internalFormat),
					desc.width,
					desc.height,
					GL_TRUE);
			}

		}

		// Once the texture is allocated, we don't need to set the parameters again
		if (!m_textureContext.allocated&& m_context.type != GL_TEXTURE_2D_MULTISAMPLE)
		{
			glTextureParameteri(m_context.id, GL_TEXTURE_WRAP_S, EnumToValue<GLenum>(p_desc.horizontalWrap));
			glTextureParameteri(m_context.id, GL_TEXTURE_WRAP_T, EnumToValue<GLenum>(p_desc.verticalWrap));
			glTextureParameteri(m_context.id, GL_TEXTURE_MIN_FILTER, EnumToValue<GLenum>(p_desc.minFilter));
			glTextureParameteri(m_context.id, GL_TEXTURE_MAG_FILTER, EnumToValue<GLenum>(p_desc.magFilter));
		}
	}
	else
	{
		if (desc.isTextureBuffer&&desc.texBufferId!=0) {
			glDeleteBuffers(1, &desc.texBufferId);
		}
		desc = p_desc;
		//顶点索引

		glGenBuffers(1, &desc.texBufferId);
		glBindBuffer(GL_TEXTURE_BUFFER, desc.texBufferId);
		glBufferData(GL_TEXTURE_BUFFER, desc.buffetLen, desc.mutableDesc.value().data, GL_STATIC_DRAW);
	
		glBindTexture(GL_TEXTURE_BUFFER, m_context.id);
		glTexBuffer(GL_TEXTURE_BUFFER, EnumToValue<GLenum>(desc.internalFormat), desc.texBufferId);
	}


	m_textureContext.allocated = true;
}

template<>
bool Rendering::HAL::GLTexture::IsValid() const
{
	return m_textureContext.allocated;
}

template<>
bool Rendering::HAL::GLTexture::IsMutable() const
{
	assert(IsValid()&& "Cannot check if a texture is mutable before it has been allocated");
	return m_textureContext.desc.mutableDesc.has_value();
}

template<>
void Rendering::HAL::GLTexture::Upload(const void* p_data, Settings::EFormat p_format, Settings::EPixelDataType p_type)
{
	assert(IsValid()&&"Cannot upload data to a texture before it has been allocated");
	assert(p_data&&"Cannot upload texture data from a null pointer");

	//currently only support for rbga with float and byte
	if (p_format == Settings::EFormat::RGBA) {
		int size = 0;
		if (p_type == Settings::EPixelDataType::FLOAT) {
			size  =(m_textureContext.desc.height * m_textureContext.desc.width * 16);
			texData.resize(size);
			memcpy(texData.data(), p_data, size);
		}
		if (p_type == Settings::EPixelDataType::UNSIGNED_BYTE) {
			size = (m_textureContext.desc.height * m_textureContext.desc.width *4);
			texData.resize(size);
			memcpy(texData.data(),p_data,size);
		}
		
	}
	;
	if (IsMutable())
	{
		m_textureContext.desc.mutableDesc.value().data = p_data;
		Allocate(m_textureContext.desc);
	}
	else
	{
		if (m_context.type == GL_TEXTURE_CUBE_MAP)
		{
			for (uint32_t i = 0; i < 6; ++i)
			{
				glTextureSubImage3D(
					m_context.id,
					0,
					0,
					0,
					0,
					m_textureContext.desc.width,
					m_textureContext.desc.height,
					i,
					EnumToValue<GLenum>(p_format),
					EnumToValue<GLenum>(p_type),
					p_data
				);
			}
		}
		else
		{
			glTextureSubImage2D(
				m_context.id,
				0,
				0,
				0,
				m_textureContext.desc.width,
				m_textureContext.desc.height,
				EnumToValue<GLenum>(p_format),
				EnumToValue<GLenum>(p_type),
				p_data
			);
		}
	}
}

template<>
void Rendering::HAL::GLTexture::Resize(uint32_t p_width, uint32_t p_height)
{
	assert(IsValid()&&"Cannot resize a texture before it has been allocated");
	assert(IsMutable()&&"Cannot resize an immutable texture");

	auto& desc = m_textureContext.desc;

	if (p_width != desc.width || p_height != desc.height)
	{
		desc.width = p_width;
		desc.height = p_height;

		Allocate(desc);
	}
}

template<>
const Rendering::Settings::TextureDesc& Rendering::HAL::GLTexture::GetDesc() const
{
	assert(IsValid()&&"Cannot get the descriptor of a texture before it has been allocated");
	return m_textureContext.desc;
}

template<>
void Rendering::HAL::GLTexture::GenerateMipmaps() const
{
    assert(IsValid()&&"Cannot generate mipmaps for a texture before it has been allocated");
	assert(m_textureContext.desc.useMipMaps&&"Cannot generate mipmaps for a texture that doesn't use them");

	if (IsValidMipMapFilter(m_textureContext.desc.minFilter))
	{
		glGenerateTextureMipmap(m_context.id);
	}
	else
	{
		// In the event a user tries to generate mipmaps for a texture that doesn't use a valid mipmap filter
		std::cout << "Cannot generate mipmaps for a texture that doesn't use a valid mipmap filter" << std::endl;
	}
}

template<>
void Rendering::HAL::GLTexture::SetBorderColor(const Maths::FVector4& p_color)
{
	assert(IsValid()&&"Cannot set border color for a texture before it has been allocated");
	glTextureParameterfv(m_context.id, GL_TEXTURE_BORDER_COLOR, &p_color.x);
}

template<>
int Rendering::HAL::GLTexture::GetWidth()
{
	return m_textureContext.desc.width;
}
template<>
int Rendering::HAL::GLTexture::GetHeight()
{
	return m_textureContext.desc.height;
}
template<>
const std::string& Rendering::HAL::GLTexture::GetDebugName() const
{
	return m_textureContext.debugName;
}
