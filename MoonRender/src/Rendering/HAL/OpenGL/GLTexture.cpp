#include <glad/glad.h>
#include <Rendering/HAL/OpenGL/GLTexture.h>
#include <Rendering/HAL/OpenGL/GLTypes.h>
#include <assert.h>
#include <iostream>

#include <Rendering/Backend/BackendResources.h>
#include <Rendering/Backend/CreateTextureRequest.h>
#include <Rendering/Backend/TextureRequests.h>
#include <Rendering/Backend/TextureUploadRequest.h>
#include <Rendering/Backend/UpdateTextureRequest.h>
#include <Rendering/Context/Driver.h>
#include <Rendering/HAL/Common/HALDriverAccess.h>

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
	m_textureContext.debugName = p_debugName;

	::Rendering::Context::Driver* const driver = ::Rendering::HAL::CurrentDriver();
	if (driver != nullptr && !driver->IsDriverThread())
	{
		// Front end with a render thread: the GL name belongs to the side that owns the context, so
		// this object is only the handle that names the slot there (Filament's Texture, and its
		// createTextureS). Everything below - allocate, upload, attach, bind - is recorded.
		m_context.handleId = ::Rendering::Backend::GetHandleAllocator()
			.Allocate<::Rendering::HAL::Texture>().GetId();

		::Rendering::Backend::CreateTextureRequest request;
		request.handleId = m_context.handleId;
		request.type = p_type;
		request.debugName = std::string(p_debugName);
		driver->CreateTexture(std::move(request));
		// Resource creation is synchronous for the caller: what names this texture next (a
		// framebuffer attachment, a material instance) does so from this side of the frame.
		driver->ExecutePendingCommands();
	}
	else
	{
		glCreateTextures(m_context.type, 1, &m_context.id);
	}

	CreationEvent.Invoke(*this);
}

template<>
Rendering::HAL::GLTexture::TTexture(Settings::ETextureType p_type,
	std::string_view p_debugName, BackendConstructionTag) : GLTextureHandle(p_type)
{
	// The backend object: the context is current on this thread, so the name is made here.
	glCreateTextures(m_context.type, 1, &m_context.id);
	m_textureContext.debugName = p_debugName;
	CreationEvent.Invoke(*this);
}

template<>
Rendering::HAL::GLTexture::~TTexture()
{
	if (m_context.id != 0)
	{
		if (m_textureContext.desc.isTextureBuffer&& m_textureContext.desc.texBufferId!=0) {
			glDeleteBuffers(1, &m_textureContext.desc.texBufferId);
		}
		glDeleteTextures(1, &m_context.id);
	}
	else if (m_context.handleId != 0)
	{
		// The name lives in the slot: freeing it is the backend object's destructor, which runs on
		// the side that owns the context (see Records::DestroyTexture).
		::Rendering::Backend::Records::DestroyTexture(
			::Rendering::Backend::ResourceHandle<::Rendering::HAL::Texture>{ m_context.handleId });
	}
	DestructionEvent.Invoke(*this);
}

template<>
void Rendering::HAL::GLTexture::Allocate(const Settings::TextureDesc& p_desc)
{
	auto& desc = m_textureContext.desc;

	if (m_context.id == 0 && m_context.handleId != 0
		&& ::Rendering::HAL::CurrentDriver() != nullptr)
	{
		// Recorded: the executor allocates the storage. This side only remembers the description,
		// which is what GetDesc() and the material descriptors read.
		desc = p_desc;
		desc.width = std::max(1u, desc.width);
		desc.height = std::max(1u, desc.height);

		::Rendering::Backend::CreateTextureRequest request;
		request.handleId = m_context.handleId;
		request.type = GetType();
		request.debugName = m_textureContext.debugName;
		request.allocate = true;
		request.descriptor = desc;

		// The descriptor's data pointer names the caller's memory - a local vector of domain
		// colours (CTopoShape's faces), the sketch's edge values, a temporary image. By the time the
		// command runs that memory is gone, and a texture buffer allocated from it holds whatever
		// was written there next, which is how a face ends up with another face's colour. Carry the
		// bytes with the command (Filament's BufferDescriptor owns its copy for the same reason) and
		// the executor points the descriptor at them.
		if (desc.mutableDesc.has_value() && desc.mutableDesc->data != nullptr && desc.buffetLen > 0)
		{
			const auto* const begin = static_cast<const uint8_t*>(desc.mutableDesc->data);
			request.pixels.assign(begin, begin + desc.buffetLen);
		}

		::Rendering::HAL::CurrentDriver()->CreateTexture(std::move(request));
		m_textureContext.allocated = true;
		return;
	}

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

	if (m_context.id == 0 && m_context.handleId != 0 && ::Rendering::HAL::CurrentDriver() != nullptr)
	{
		// Recorded: the bytes travel with the command (Filament's PixelBufferDescriptor), so the
		// caller's buffer is free the moment this returns. The executor allocates and uploads.
		const auto& desc = m_textureContext.desc;
		const size_t components = p_format == Settings::EFormat::RGBA ? 4u : 1u;
		const size_t bytesPerComponent = p_type == Settings::EPixelDataType::FLOAT ? 4u
			: p_type == Settings::EPixelDataType::UNSIGNED_BYTE ? 1u : 2u;
		const size_t faces = GetType() == Settings::ETextureType::TEXTURE_CUBE ? 6u : 1u;
		const size_t size = static_cast<size_t>(desc.width) * desc.height * faces
			* components * bytesPerComponent;

		::Rendering::Backend::TextureUploadRequest request;
		request.handleId = m_context.handleId;
		request.descriptor = desc;
		request.format = p_format;
		request.type = p_type;
		request.pixels.assign(static_cast<const uint8_t*>(p_data),
			static_cast<const uint8_t*>(p_data) + size);
		::Rendering::HAL::CurrentDriver()->UploadTexture(std::move(request));
		return;
	}

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

	if (m_context.id == 0 && m_context.handleId != 0
		&& ::Rendering::HAL::CurrentDriver() != nullptr)
	{
		// Recorded: glGenerateTextureMipmap needs the name, i.e. the executor.
		::Rendering::Backend::GenerateTextureMipmapsRequest request;
		request.handleId = m_context.handleId;
		::Rendering::HAL::CurrentDriver()->EnqueueGenerateTextureMipmaps(request);
		return;
	}

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

	if (m_context.id == 0 && m_context.handleId != 0
		&& ::Rendering::HAL::CurrentDriver() != nullptr)
	{
		::Rendering::Backend::SetTextureBorderColorRequest request;
		request.handleId = m_context.handleId;
		request.color = p_color;
		::Rendering::HAL::CurrentDriver()->EnqueueSetTextureBorderColor(request);
		return;
	}

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
	if (m_context.id == 0 && m_context.handleId != 0 && ::Rendering::HAL::CurrentDriver() != nullptr)
	{
		const size_t components = p_format == Settings::EFormat::RGBA ? 4u : 1u;
		const size_t bytesPerComponent = p_type == Settings::EPixelDataType::FLOAT ? 4u
			: p_type == Settings::EPixelDataType::UNSIGNED_BYTE ? 1u : 2u;
		const size_t size = static_cast<size_t>(p_width) * p_height * components * bytesPerComponent;

		::Rendering::Backend::UpdateTextureRequest request;
		request.handleId = m_context.handleId;
		request.x = p_x;
		request.y = p_y;
		request.width = p_width;
		request.height = p_height;
		request.format = p_format;
		request.type = p_type;
		request.pixels.assign(static_cast<const uint8_t*>(p_data),
			static_cast<const uint8_t*>(p_data) + size);
		::Rendering::HAL::CurrentDriver()->UpdateTexture(std::move(request));
		return;
	}

	glTextureSubImage2D(
		m_context.id,
		0,
		static_cast<GLint>(p_x),
		static_cast<GLint>(p_y),
		static_cast<GLsizei>(p_width),
		static_cast<GLsizei>(p_height),
		EnumToValue<GLenum>(p_format),
		EnumToValue<GLenum>(p_type),
		p_data);
}