#include <Rendering/Data/VertexArrayObject.h>

#include <memory>
#include <vector>

#include <Rendering/Backend/BackendResources.h>
#include <Rendering/Backend/HandleAllocator.h>
#include <Rendering/Backend/VertexArrayRequests.h>
#include <Rendering/Context/Driver.h>
#include <Rendering/HAL/VertexArray.h>

namespace Rendering::Data
{
	using Rendering::Backend::BuildVertexLayout;

	VertexArrayObject::VertexArrayObject()
	{
		// Filament's createVertexArray: the driver reserves the handle and records the create that
		// gives it its backend object on the side that owns the context.
		m_handle = Rendering::Context::Driver::Get().CreateVertexArrayObject("");
	}

	VertexArrayObject::~VertexArrayObject()
	{
		// Teardown through the records: the GL name is queued to the side that owns the context, so
		// this is safe from any thread and after the module is gone (see Data::BufferObject).
		Rendering::Backend::Records::DestroyVertexArray(m_handle);
	}

	VertexArrayObject::VertexArrayObject(VertexArrayObject&& p_other) noexcept
		: m_handle(p_other.m_handle)
		, m_attributeCount(p_other.m_attributeCount)
	{
		p_other.m_handle.clear();
		p_other.m_attributeCount = 0;
	}

	VertexArrayObject& VertexArrayObject::operator=(VertexArrayObject&& p_other) noexcept
	{
		if (this != &p_other)
		{
			Rendering::Backend::Records::DestroyVertexArray(m_handle);

			m_handle = p_other.m_handle;
			m_attributeCount = p_other.m_attributeCount;

			p_other.m_handle.clear();
			p_other.m_attributeCount = 0;
		}
		return *this;
	}

	void VertexArrayObject::SetLayout(
		const Rendering::Backend::ResourceHandle<Rendering::Backend::HwBufferObject>& p_vertexBuffer,
		const Rendering::Backend::ResourceHandle<Rendering::Backend::HwBufferObject>& p_indexBuffer,
		Settings::VertexAttributeLayout p_attributes)
	{
		if (!static_cast<bool>(m_handle) || !static_cast<bool>(p_vertexBuffer) || p_attributes.empty())
		{
			return;
		}

		// The array and its two buffers travel as handles: the create commands that make their GL
		// names are earlier in the batch, and the executor resolves them when this runs.
		Rendering::Context::Driver::Get().SetVertexArrayObjectLayout(
			m_handle, p_vertexBuffer, p_indexBuffer, BuildVertexLayout(p_attributes));

		m_attributeCount += static_cast<uint32_t>(p_attributes.size());
	}

	void VertexArrayObject::Bind() const
	{
		// Immediate path only: the draw commands bind the array they draw from themselves.
		if (Rendering::HAL::VertexArray* const array =
			Rendering::Backend::GetHandleAllocator().HandleCast<Rendering::HAL::VertexArray>(m_handle))
		{
			array->Bind();
		}
	}

	void VertexArrayObject::Unbind() const
	{
		if (Rendering::HAL::VertexArray* const array =
			Rendering::Backend::GetHandleAllocator().HandleCast<Rendering::HAL::VertexArray>(m_handle))
		{
			array->Unbind();
		}
	}

	uint32_t VertexArrayObject::GetID() const
	{
		return Rendering::Backend::Records::VertexArrayName(m_handle);
	}
}
