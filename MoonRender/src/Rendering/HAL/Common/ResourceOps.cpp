#include <Rendering/HAL/Common/ResourceOps.h>

#include <Rendering/Backend/BufferRequests.h>
#include <Rendering/Backend/VertexArrayRequests.h>
#include <Rendering/Context/Driver.h>
#include <Rendering/HAL/Common/HALDriverAccess.h>
#include <Rendering/HAL/OpenGL/GLTypes.h>

#include <glad/glad.h>

#include <memory>
#include <vector>

namespace Rendering::HAL::ResourceOps
{
	namespace
	{
		Rendering::Context::Driver* CurrentDriver()
		{
			return ::Rendering::HAL::CurrentDriver();
		}

		/** Whether the executor is another thread: then everything that needs the context has to
		 * be recorded, because this side has no context to use. */
		bool IsRecorded(Rendering::Context::Driver* p_driver)
		{
			return p_driver != nullptr && p_driver->IsRenderThreadActive();
		}

		uint32_t GetDataTypeSizeInBytes(Rendering::Settings::EDataType p_type)
		{
			switch (p_type)
			{
			case Rendering::Settings::EDataType::BYTE: return sizeof(GLbyte);
			case Rendering::Settings::EDataType::UNSIGNED_BYTE: return sizeof(GLubyte);
			case Rendering::Settings::EDataType::SHORT: return sizeof(GLshort);
			case Rendering::Settings::EDataType::UNSIGNED_SHORT: return sizeof(GLushort);
			case Rendering::Settings::EDataType::INT: return sizeof(GLint);
			case Rendering::Settings::EDataType::UNSIGNED_INT: return sizeof(GLuint);
			case Rendering::Settings::EDataType::FLOAT: return sizeof(GLfloat);
			case Rendering::Settings::EDataType::DOUBLE: return sizeof(GLdouble);
			default: return 0;
			}
		}
	}

	void EnsureBuffer(const Rendering::Backend::ResourceHandle<Rendering::Backend::HwBufferObject>& p_handle)
	{
		using namespace Rendering::Backend;

		if (!p_handle || Records::BufferName(p_handle) != 0)
		{
			return;
		}

		if (Rendering::Context::Driver* const driver = CurrentDriver())
		{
			// The name is made on the executing side; the drain is what makes it exist before the
			// caller records anything that names it (the same call the HAL buffer made).
			driver->EnqueueCreateBuffer(CreateBufferRequest{ p_handle.GetId(), Rendering::Settings::EBufferType::VERTEX });
			driver->ExecutePendingCommands();
			return;
		}

		Records::CreateBuffer(p_handle);
	}

	void EnsureVertexArray(const Rendering::Backend::ResourceHandle<Rendering::Backend::HwVertexArray>& p_handle)
	{
		using namespace Rendering::Backend;

		if (!p_handle || Records::VertexArrayName(p_handle) != 0)
		{
			return;
		}

		if (Rendering::Context::Driver* const driver = CurrentDriver())
		{
			driver->EnqueueCreateVertexArray(p_handle.GetId());
			driver->ExecutePendingCommands();
			return;
		}

		Records::CreateVertexArray(p_handle);
	}

	void AllocateBuffer(
		const Rendering::Backend::ResourceHandle<Rendering::Backend::HwBufferObject>& p_handle,
		uint64_t p_size,
		Rendering::Settings::EAccessSpecifier p_usage)
	{
		using namespace Rendering::Backend;

		if (!p_handle || p_size == 0)
		{
			return;
		}

		if (Rendering::Context::Driver* const driver = CurrentDriver(); IsRecorded(driver))
		{
			AllocateBufferRequest request;
			request.handleId = p_handle.GetId();
			request.size = p_size;
			request.usage = p_usage;
			driver->EnqueueAllocateBuffer(request);
			return;
		}

		const uint32_t name = Records::BufferName(p_handle);
		if (name != 0)
		{
			glNamedBufferData(name, p_size, nullptr, EnumToValue<GLenum>(p_usage));
		}
	}

	void UploadBuffer(
		const Rendering::Backend::ResourceHandle<Rendering::Backend::HwBufferObject>& p_handle,
		const void* p_data,
		uint64_t p_offset,
		uint64_t p_size)
	{
		using namespace Rendering::Backend;

		if (!p_handle || p_data == nullptr || p_size == 0)
		{
			return;
		}

		const uint32_t name = Records::BufferName(p_handle);
		if (name == 0)
		{
			return;
		}

		if (Rendering::Context::Driver* const driver = CurrentDriver())
		{
			// Recorded, not executed: the bytes travel with the command (Filament's
			// BufferDescriptor) and the ring runs it where the context is.
			driver->UpdateBufferRange(name, static_cast<uint32_t>(p_offset), p_data, static_cast<uint32_t>(p_size));
			return;
		}

		glNamedBufferSubData(name, p_offset, p_size, p_data);
	}

	void SetVertexArrayLayout(
		const Rendering::Backend::ResourceHandle<Rendering::Backend::HwVertexArray>& p_array,
		const Rendering::Backend::ResourceHandle<Rendering::Backend::HwBufferObject>& p_vertexBuffer,
		const Rendering::Backend::ResourceHandle<Rendering::Backend::HwBufferObject>& p_indexBuffer,
		Rendering::Settings::VertexAttributeLayout p_attributes)
	{
		using namespace Rendering::Backend;

		if (!p_array || !p_vertexBuffer || p_attributes.empty())
		{
			return;
		}

		if (!IsRecorded(CurrentDriver()))
		{
			// Inline: bind, describe every attribute, unbind (what this did before there was a
			// command for it).
			glBindVertexArray(Records::VertexArrayName(p_array));
			const uint32_t indexBufferName = Records::BufferName(p_indexBuffer);
			if (indexBufferName != 0)
			{
				glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBufferName);
			}
			glBindBuffer(GL_ARRAY_BUFFER, Records::BufferName(p_vertexBuffer));

			uint32_t stride = 0;
			for (const Rendering::Settings::VertexAttribute& attribute : p_attributes)
			{
				stride += GetDataTypeSizeInBytes(attribute.type) * attribute.count;
			}

			uint64_t offset = 0;
			uint32_t index = 0;
			for (const Rendering::Settings::VertexAttribute& attribute : p_attributes)
			{
				glEnableVertexAttribArray(index);
				glVertexAttribPointer(index,
					static_cast<GLint>(attribute.count),
					EnumToValue<GLenum>(attribute.type),
					static_cast<GLboolean>(attribute.normalized),
					static_cast<GLsizei>(stride),
					reinterpret_cast<const void*>(static_cast<uintptr_t>(offset)));
				offset += GetDataTypeSizeInBytes(attribute.type) * attribute.count;
				++index;
			}

			glBindVertexArray(0);
			return;
		}

		auto attributes = std::make_shared<std::vector<VertexLayoutAttribute>>();
		attributes->reserve(p_attributes.size());

		uint32_t stride = 0;
		for (const Rendering::Settings::VertexAttribute& attribute : p_attributes)
		{
			stride += GetDataTypeSizeInBytes(attribute.type) * attribute.count;
		}

		uint64_t offset = 0;
		for (const Rendering::Settings::VertexAttribute& attribute : p_attributes)
		{
			VertexLayoutAttribute entry;
			entry.type = attribute.type;
			entry.count = attribute.count;
			entry.normalized = attribute.normalized;
			entry.stride = stride;
			entry.offset = offset;
			attributes->push_back(entry);
			offset += GetDataTypeSizeInBytes(attribute.type) * attribute.count;
		}

		SetVertexArrayLayoutRequest request;
		request.vertexArrayId = Records::VertexArrayName(p_array);
		request.vertexBufferId = Records::BufferName(p_vertexBuffer);
		request.indexBufferId = Records::BufferName(p_indexBuffer);
		request.attributes = std::move(attributes);
		CurrentDriver()->ApplyVertexArrayLayout(request);
	}

	void DestroyBuffer(const Rendering::Backend::ResourceHandle<Rendering::Backend::HwBufferObject>& p_handle)
	{
		Rendering::Backend::Records::DestroyBuffer(p_handle);
	}

	void DestroyVertexArray(const Rendering::Backend::ResourceHandle<Rendering::Backend::HwVertexArray>& p_handle)
	{
		Rendering::Backend::Records::DestroyVertexArray(p_handle);
	}
}
