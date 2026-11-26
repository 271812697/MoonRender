#include <Rendering/Data/Texture.h>

#include <algorithm>
#include <cstring>

#include <Rendering/Backend/BackendResources.h>
#include <Rendering/Backend/PixelBufferDescriptor.h>
#include <Rendering/Context/Driver.h>

namespace Rendering::Data
{
	namespace
	{
		/** The number of bytes one pixel of p_format/p_type takes: the CPU mirror's size, and the
		 * size of a sub-image upload. */
		size_t PixelBytes(
			Rendering::Settings::EFormat p_format,
			Rendering::Settings::EPixelDataType p_type)
		{
			const bool isFloat = p_type == Rendering::Settings::EPixelDataType::FLOAT;
			return (p_format == Rendering::Settings::EFormat::RGBA ? 4u : 1u) * (isFloat ? 4u : 1u);
		}
	}

	Texture::Texture(Settings::ETextureType p_type, std::string_view p_debugName)
		: m_type(p_type)
		, m_debugName(p_debugName)
	{
		// Filament's createTexture: the driver reserves the handle - it is what every later command
		// names, and it exists before the object does - and records the create that gives it its
		// backend object on the side that owns the context.
		m_handle = Rendering::Context::Driver::Get().CreateTextureObject(p_type, m_debugName);
	}

	Texture::~Texture()
	{
		if (static_cast<bool>(m_handle))
		{
			// Teardown goes through the records, not the driver: the slot is freed here and the GL
			// name is queued to the side that owns the context, so this is safe from any thread and
			// after the module is gone (see Data::BufferObject).
			Rendering::Backend::Records::DestroyTexture(m_handle);
			m_handle.clear();
		}
	}

	Texture::Texture(Texture&& p_other) noexcept
		: m_handle(p_other.m_handle)
		, m_type(p_other.m_type)
		, m_desc(std::move(p_other.m_desc))
		, m_allocated(p_other.m_allocated)
		, m_debugName(std::move(p_other.m_debugName))
		, texData(std::move(p_other.texData))
	{
		p_other.m_handle.clear();
		p_other.m_allocated = false;
	}

	Texture& Texture::operator=(Texture&& p_other) noexcept
	{
		if (this != &p_other)
		{
			if (static_cast<bool>(m_handle))
			{
				Rendering::Backend::Records::DestroyTexture(m_handle);
			}

			m_handle = p_other.m_handle;
			m_type = p_other.m_type;
			m_desc = std::move(p_other.m_desc);
			m_allocated = p_other.m_allocated;
			m_debugName = std::move(p_other.m_debugName);
			texData = std::move(p_other.texData);

			p_other.m_handle.clear();
			p_other.m_allocated = false;
		}
		return *this;
	}

	void Texture::Allocate(const Settings::TextureDesc& p_desc)
	{
		if (!static_cast<bool>(m_handle))
		{
			return;
		}

		m_desc = p_desc;
		m_desc.width = std::max(1u, m_desc.width);
		m_desc.height = std::max(1u, m_desc.height);

		// A mutable texture's contents travel with the allocation: the descriptor's own data pointer
		// names the caller's memory (a local vector of domain colours, the sketch's edge values),
		// which is gone or rewritten by the time the executor runs.
		Rendering::Backend::PixelBufferDescriptor pixels;
		if (m_desc.mutableDesc.has_value() && m_desc.mutableDesc->data != nullptr
			&& m_desc.buffetLen > 0)
		{
			pixels = Rendering::Backend::PixelBufferDescriptor::Own(
				m_desc.mutableDesc->data, m_desc.buffetLen,
				m_desc.mutableDesc->format, m_desc.mutableDesc->type);
		}

		Rendering::Context::Driver::Get().AllocateTextureObject(m_handle, m_desc, std::move(pixels));
		m_allocated = true;
	}

	void Texture::Upload(const void* p_data, Settings::EFormat p_format, Settings::EPixelDataType p_type)
	{
		if (p_data == nullptr || !static_cast<bool>(m_handle))
		{
			return;
		}

		// The CPU mirror the path tracer's BVH bakes its maps from (BvhService reads texData): the
		// pixels only exist on this side for as long as the call lasts.
		if (p_format == Settings::EFormat::RGBA)
		{
			const size_t size = static_cast<size_t>(m_desc.width) * m_desc.height
				* PixelBytes(p_format, p_type);
			texData.resize(size);
			std::memcpy(texData.data(), p_data, size);
		}

		const size_t faces = m_type == Settings::ETextureType::TEXTURE_CUBE ? 6u : 1u;
		const size_t size = static_cast<size_t>(m_desc.width) * m_desc.height * faces
			* PixelBytes(p_format, p_type);

		// One command carries the bytes (Filament's PixelBufferDescriptor): the caller's buffer is
		// free the moment this returns, and the executor allocates the storage when it has none.
		Rendering::Context::Driver::Get().UploadTextureObject(
			m_handle, m_desc,
			Rendering::Backend::PixelBufferDescriptor::Own(p_data, size, p_format, p_type),
			false);

		m_allocated = true;
	}

	void Texture::UploadSubImage(
		const void* p_data,
		Settings::EFormat p_format,
		Settings::EPixelDataType p_type,
		uint32_t p_x,
		uint32_t p_y,
		uint32_t p_width,
		uint32_t p_height)
	{
		if (p_data == nullptr || !static_cast<bool>(m_handle))
		{
			return;
		}

		const size_t size = static_cast<size_t>(p_width) * p_height * PixelBytes(p_format, p_type);
		Rendering::Context::Driver::Get().UpdateTextureObject(
			m_handle, p_x, p_y, p_width, p_height,
			Rendering::Backend::PixelBufferDescriptor::Own(p_data, size, p_format, p_type));
	}

	void Texture::Resize(uint32_t p_width, uint32_t p_height)
	{
		if (m_desc.width == p_width && m_desc.height == p_height)
		{
			return;
		}

		Allocate({
			.width = p_width,
			.height = p_height,
			.minFilter = m_desc.minFilter,
			.magFilter = m_desc.magFilter,
			.horizontalWrap = m_desc.horizontalWrap,
			.verticalWrap = m_desc.verticalWrap,
			.internalFormat = m_desc.internalFormat,
			.useMipMaps = m_desc.useMipMaps,
			.mipLevels = m_desc.mipLevels,
			.isTextureBuffer = m_desc.isTextureBuffer,
			.buffetLen = m_desc.buffetLen,
			.mutableDesc = m_desc.mutableDesc
		});
	}

	void Texture::GenerateMipmaps() const
	{
		if (!static_cast<bool>(m_handle))
		{
			return;
		}

		// glGenerateTextureMipmap needs the name, i.e. the executor (Filament's generateMipmaps).
		Rendering::Context::Driver::Get().GenerateTextureMipmapsObject(m_handle);
	}

	void Texture::SetBorderColor(const Maths::FVector4& p_color)
	{
		if (!static_cast<bool>(m_handle))
		{
			return;
		}

		Rendering::Context::Driver::Get().SetTextureBorderColorObject(m_handle, p_color);
	}

	const Settings::TextureDesc& Texture::GetDesc() const
	{
		return m_desc;
	}

	uint32_t Texture::GetID() const
	{
		return Rendering::Backend::Records::TextureName(m_handle.GetId());
	}
}
