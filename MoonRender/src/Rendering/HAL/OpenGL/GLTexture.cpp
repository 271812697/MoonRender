#include <glad/glad.h>
#include <Rendering/HAL/OpenGL/GLTexture.h>
#include <Rendering/HAL/OpenGL/GLTypes.h>
#include <assert.h>
#include <iostream>

namespace
{
	uint32_t CalculateMipMapLevels(uint32_t p_width, uint32_t p_height)
	{
		uint32_t maxDim = p_width > p_height ? p_width : p_height;
		uint32_t t = __lzcnt(maxDim);
		return maxDim ? 32u - t : 1u;
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
Rendering::HAL::GLTexture::TTexture(Settings::ETextureType p_type, std::string_view p_debugName,
	BackendConstructionTag) : HwTexture(p_type)
{
	// The backend object: the context is current on this thread, so the name is made here.
	this->debugName = p_debugName;
	glCreateTextures(EnumToValue<GLenum>(p_type), 1, &m_textureContext.glId);
}

template<>
Rendering::HAL::GLTexture::~TTexture()
{
	if (m_textureContext.glId != 0)
	{
		if (this->desc.isTextureBuffer && this->desc.texBufferId != 0)
		{
			glDeleteBuffers(1, &this->desc.texBufferId);
		}
		glDeleteTextures(1, &m_textureContext.glId);
		m_textureContext.glId = 0;
	}
}

template<>
void Rendering::HAL::GLTexture::Allocate(const Settings::TextureDesc& p_desc)
{
	auto& desc = this->desc;

	if (!p_desc.isTextureBuffer)
	{
		desc = p_desc;
		desc.width = std::max(1u, desc.width);
		desc.height = std::max(1u, desc.height);

		if (desc.mutableDesc.has_value())
		{
			const auto& mutableDesc = desc.mutableDesc.value();

			assert(this->type != Settings::ETextureType::TEXTURE_CUBE
				&& "Mutable textures are only supported for 2D textures");

			// No DSA version for glTexImage2D (mutable texture),
			// so we need to Bind/Unbind the texture.
			Bind();
			if (this->type == Settings::ETextureType::TEXTURE_2D)
			{
				glTexImage2D(
					EnumToValue<GLenum>(this->type),
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
			else if (this->type == Settings::ETextureType::TEXTURE_2DMULSAMPLE)
			{
				glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, 4,
					EnumToValue<GLenum>(desc.internalFormat), desc.width, desc.height, GL_TRUE);
			}
			else if (this->type == Settings::ETextureType::TEXTURE_2DARRAY)
			{
				glTexImage3D(
					EnumToValue<GLenum>(this->type),
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
			if (this->type != Settings::ETextureType::TEXTURE_2DMULSAMPLE)
			{
				glTextureStorage2D(
					m_textureContext.glId,
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
					m_textureContext.glId,
					4,
					EnumToValue<GLenum>(desc.internalFormat),
					desc.width,
					desc.height,
					GL_TRUE);
			}
		}

		// Once the texture is allocated, we don't need to set the parameters again
		if (!this->allocated && this->type != Settings::ETextureType::TEXTURE_2DMULSAMPLE)
		{
			glTextureParameteri(m_textureContext.glId, GL_TEXTURE_WRAP_S,
				EnumToValue<GLenum>(p_desc.horizontalWrap));
			glTextureParameteri(m_textureContext.glId, GL_TEXTURE_WRAP_T,
				EnumToValue<GLenum>(p_desc.verticalWrap));
			glTextureParameteri(m_textureContext.glId, GL_TEXTURE_MIN_FILTER,
				EnumToValue<GLenum>(p_desc.minFilter));
			glTextureParameteri(m_textureContext.glId, GL_TEXTURE_MAG_FILTER,
				EnumToValue<GLenum>(p_desc.magFilter));
		}
	}
	else
	{
		if (desc.isTextureBuffer && desc.texBufferId != 0)
		{
			glDeleteBuffers(1, &desc.texBufferId);
		}
		desc = p_desc;
		//顶点索引

		glGenBuffers(1, &desc.texBufferId);
		glBindBuffer(GL_TEXTURE_BUFFER, desc.texBufferId);
		glBufferData(GL_TEXTURE_BUFFER, desc.buffetLen, desc.mutableDesc.value().data, GL_STATIC_DRAW);

		glBindTexture(GL_TEXTURE_BUFFER, m_textureContext.glId);
		glTexBuffer(GL_TEXTURE_BUFFER, EnumToValue<GLenum>(desc.internalFormat), desc.texBufferId);
	}

	this->allocated = true;
}

template<>
bool Rendering::HAL::GLTexture::IsValid() const
{
	return this->allocated;
}

template<>
bool Rendering::HAL::GLTexture::IsMutable() const
{
	assert(IsValid() && "Cannot check if a texture is mutable before it has been allocated");
	return this->desc.mutableDesc.has_value();
}

template<>
void Rendering::HAL::GLTexture::Upload(const void* p_data, Settings::EFormat p_format,
	Settings::EPixelDataType p_type)
{
	assert(IsValid() && "Cannot upload data to a texture before it has been allocated");
	assert(p_data && "Cannot upload texture data from a null pointer");

	if (IsMutable())
	{
		this->desc.mutableDesc.value().data = p_data;
		Allocate(this->desc);
	}
	else
	{
		if (this->type == Settings::ETextureType::TEXTURE_CUBE)
		{
			for (uint32_t i = 0; i < 6; ++i)
			{
				glTextureSubImage3D(
					m_textureContext.glId,
					0,
					0,
					0,
					0,
					this->desc.width,
					this->desc.height,
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
				m_textureContext.glId,
				0,
				0,
				0,
				this->desc.width,
				this->desc.height,
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
	assert(IsValid() && "Cannot resize a texture before it has been allocated");
	assert(IsMutable() && "Cannot resize an immutable texture");

	auto& desc = this->desc;

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
	assert(IsValid() && "Cannot get the descriptor of a texture before it has been allocated");
	return this->desc;
}

template<>
void Rendering::HAL::GLTexture::GenerateMipmaps() const
{
	assert(IsValid() && "Cannot generate mipmaps for a texture before it has been allocated");
	assert(this->desc.useMipMaps && "Cannot generate mipmaps for a texture that doesn't use them");

	if (IsValidMipMapFilter(this->desc.minFilter))
	{
		glGenerateTextureMipmap(m_textureContext.glId);
	}
	else
	{
		// In the event a user tries to generate mipmaps for a texture that doesn't use a valid
		// mipmap filter
		std::cout << "Cannot generate mipmaps for a texture that doesn't use a valid mipmap filter"
			<< std::endl;
	}
}

template<>
void Rendering::HAL::GLTexture::SetBorderColor(const Maths::FVector4& p_color)
{
	assert(IsValid() && "Cannot set border color for a texture before it has been allocated");
	glTextureParameterfv(m_textureContext.glId, GL_TEXTURE_BORDER_COLOR, &p_color.x);
}

template<>
int Rendering::HAL::GLTexture::GetWidth()
{
	return static_cast<int>(this->desc.width);
}

template<>
int Rendering::HAL::GLTexture::GetHeight()
{
	return static_cast<int>(this->desc.height);
}

template<>
const std::string& Rendering::HAL::GLTexture::GetDebugName() const
{
	return this->debugName;
}

template<>
void Rendering::HAL::GLTexture::Bind(uint32_t p_slot) const
{
	glBindTextureUnit(p_slot, m_textureContext.glId);
}

template<>
void Rendering::HAL::GLTexture::Bind() const
{
	glBindTexture(EnumToValue<GLenum>(this->type), m_textureContext.glId);
}

template<>
void Rendering::HAL::GLTexture::Unbind() const
{
	glBindTexture(EnumToValue<GLenum>(this->type), 0);
}

template<>
uint32_t Rendering::HAL::GLTexture::GetID() const
{
	return m_textureContext.glId;
}

template<>
void Rendering::HAL::GLTexture::UploadSubImage(
	const void* p_data,
	Settings::EFormat p_format,
	Settings::EPixelDataType p_type,
	uint32_t p_x,
	uint32_t p_y,
	uint32_t p_width,
	uint32_t p_height)
{
	assert(IsValid() && "Cannot upload data to a texture before it has been allocated");
	assert(p_data && "Cannot upload texture data from a null pointer");

	// A rectangle inside storage that already exists: this is the update path for runtime-generated
	// images (the view cube's atlas after a language or theme switch), not a re-allocation.
	glTextureSubImage2D(
		m_textureContext.glId,
		0,
		static_cast<GLint>(p_x),
		static_cast<GLint>(p_y),
		static_cast<GLsizei>(p_width),
		static_cast<GLsizei>(p_height),
		EnumToValue<GLenum>(p_format),
		EnumToValue<GLenum>(p_type),
		p_data);
}
