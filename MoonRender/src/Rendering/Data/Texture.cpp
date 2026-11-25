#include <Rendering/Data/Texture.h>

#include <algorithm>
#include <cstring>

#include <Rendering/Backend/BackendResources.h>
#include <Rendering/Backend/CreateTextureRequest.h>
#include <Rendering/Backend/HandleAllocator.h>
#include <Rendering/Backend/TextureRequests.h>
#include <Rendering/Backend/TextureUploadRequest.h>
#include <Rendering/Backend/UpdateTextureRequest.h>
#include <Rendering/Context/Driver.h>
#include <Rendering/HAL/Common/HALDriverAccess.h>
#include <Rendering/HAL/Texture.h>

namespace Rendering::Data
{
	namespace
	{
		/** The backend object this handle names, on the side that owns the context; null until its
		 * create command has run (or after it was destroyed). */
		Rendering::HAL::Texture* Resolve(
			Rendering::Backend::ResourceHandle<Rendering::Backend::HwTexture> p_handle)
		{
			return Rendering::Backend::GetHandleAllocator()
				.HandleCast<Rendering::HAL::Texture>(p_handle);
		}

		/** Records the creation as a command, or - with no driver to record into - creates the
		 * object here, which is what the code did before the commands existed. */
		void EnsureBackend(
			Rendering::Backend::ResourceHandle<Rendering::Backend::HwTexture> p_handle,
			Rendering::Settings::ETextureType p_type,
			const std::string& p_debugName)
		{
			Rendering::Context::Driver* const driver = ::Rendering::HAL::CurrentDriver();
			if (driver != nullptr)
			{
				Rendering::Backend::CreateTextureRequest request;
				request.handleId = p_handle.GetId();
				request.type = p_type;
				request.debugName = p_debugName;
				driver->CreateTexture(std::move(request));
				return;
			}

			Rendering::Backend::Records::CreateTexture(p_handle, p_type, p_debugName);
		}

		/** The number of bytes one pixel of p_format/p_type takes: the CPU mirror's size. */
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
		// The handle exists before the GPU object does (Filament's Texture allocates its HwTexture
		// handle before the driver creates the object), and it is what every recorded command
		// names.
		m_handle = Rendering::Backend::GetHandleAllocator()
			.Allocate<Rendering::Backend::HwTexture>();
		EnsureBackend(m_handle, m_type, m_debugName);
	}

	Texture::~Texture()
	{
		if (static_cast<bool>(m_handle))
		{
			// The allocator owns the object and queues its destruction to the context side.
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

		Rendering::Context::Driver* const driver = ::Rendering::HAL::CurrentDriver();
		if (driver != nullptr)
		{
			// Recorded: the executor allocates the storage. This side only remembers the
			// description, which is what GetDesc() and the material descriptors read.
			Rendering::Backend::CreateTextureRequest request;
			request.handleId = m_handle.GetId();
			request.type = m_type;
			request.debugName = m_debugName;
			request.allocate = true;
			request.descriptor = m_desc;

			// The descriptor's data pointer names the caller's memory - a local vector of domain
			// colours (CTopoShape's faces), the sketch's edge values, a temporary image. By the time
			// the command runs that memory is gone, and a texture buffer allocated from it holds
			// whatever was written there next, which is how a face ends up with another face's
			// colour. Carry the bytes with the command (Filament's BufferDescriptor owns its copy
			// for the same reason) and the executor points the descriptor at them.
			if (m_desc.mutableDesc.has_value() && m_desc.mutableDesc->data != nullptr
				&& m_desc.buffetLen > 0)
			{
				const auto* const begin = static_cast<const uint8_t*>(m_desc.mutableDesc->data);
				request.pixels.assign(begin, begin + m_desc.buffetLen);
			}

			driver->CreateTexture(std::move(request));
			m_allocated = true;
			return;
		}

		if (Rendering::HAL::Texture* const target = Resolve(m_handle))
		{
			target->Allocate(m_desc);
			m_allocated = target->IsValid();
			return;
		}

		// No driver and no object yet: make it here, then allocate.
		EnsureBackend(m_handle, m_type, m_debugName);
		if (Rendering::HAL::Texture* const created = Resolve(m_handle))
		{
			created->Allocate(m_desc);
			m_allocated = created->IsValid();
		}
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

		Rendering::Context::Driver* const driver = ::Rendering::HAL::CurrentDriver();
		if (driver != nullptr)
		{
			// Recorded: the bytes travel with the command (Filament's PixelBufferDescriptor), so the
			// caller's buffer is free the moment this returns. The executor allocates and uploads.
			const size_t faces = m_type == Settings::ETextureType::TEXTURE_CUBE ? 6u : 1u;
			const size_t size = static_cast<size_t>(m_desc.width) * m_desc.height * faces
				* PixelBytes(p_format, p_type);

			Rendering::Backend::TextureUploadRequest request;
			request.handleId = m_handle.GetId();
			request.descriptor = m_desc;
			request.format = p_format;
			request.type = p_type;
			request.pixels.assign(static_cast<const uint8_t*>(p_data),
				static_cast<const uint8_t*>(p_data) + size);
			driver->UploadTexture(std::move(request));
			return;
		}

		if (Rendering::HAL::Texture* const target = Resolve(m_handle))
		{
			if (!target->IsValid() && m_allocated)
			{
				target->Allocate(m_desc);
			}
			target->Upload(p_data, p_format, p_type);
		}
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

		Rendering::Context::Driver* const driver = ::Rendering::HAL::CurrentDriver();
		if (driver != nullptr)
		{
			const size_t size = static_cast<size_t>(p_width) * p_height * PixelBytes(p_format, p_type);

			Rendering::Backend::UpdateTextureRequest request;
			request.handleId = m_handle.GetId();
			request.x = p_x;
			request.y = p_y;
			request.width = p_width;
			request.height = p_height;
			request.format = p_format;
			request.type = p_type;
			request.pixels.assign(static_cast<const uint8_t*>(p_data),
				static_cast<const uint8_t*>(p_data) + size);
			driver->UpdateTexture(std::move(request));
			return;
		}

		if (Rendering::HAL::Texture* const target = Resolve(m_handle))
		{
			target->UploadSubImage(p_data, p_format, p_type, p_x, p_y, p_width, p_height);
		}
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

		if (Rendering::Context::Driver* const driver = ::Rendering::HAL::CurrentDriver())
		{
			// Recorded: glGenerateTextureMipmap needs the name, i.e. the executor.
			Rendering::Backend::GenerateTextureMipmapsRequest request;
			request.handleId = m_handle.GetId();
			driver->EnqueueGenerateTextureMipmaps(request);
			return;
		}

		if (const Rendering::HAL::Texture* const target = Resolve(m_handle))
		{
			target->GenerateMipmaps();
		}
	}

	void Texture::SetBorderColor(const Maths::FVector4& p_color)
	{
		if (!static_cast<bool>(m_handle))
		{
			return;
		}

		if (Rendering::Context::Driver* const driver = ::Rendering::HAL::CurrentDriver())
		{
			Rendering::Backend::SetTextureBorderColorRequest request;
			request.handleId = m_handle.GetId();
			request.color = p_color;
			driver->EnqueueSetTextureBorderColor(request);
			return;
		}

		if (Rendering::HAL::Texture* const target = Resolve(m_handle))
		{
			target->SetBorderColor(p_color);
		}
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
