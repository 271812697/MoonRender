#include <Rendering/Data/VertexArrayObject.h>

#include <Rendering/Backend/BackendResources.h>
#include <Rendering/Backend/HandleAllocator.h>
#include <Rendering/HAL/Common/ResourceOps.h>
#include <Rendering/HAL/VertexArray.h>

namespace Rendering::Data
{
	namespace
	{
		Rendering::HAL::VertexArray* Resolve(
			const Rendering::Backend::ResourceHandle<Rendering::Backend::HwVertexArray>& p_handle)
		{
			return Rendering::Backend::GetHandleAllocator()
				.HandleCast<Rendering::HAL::VertexArray>(p_handle);
		}
	}

	VertexArrayObject::VertexArrayObject()
	{
		// Filament's createVertexArrayS: the slot is reserved here, without GL, and the create
		// command gives it its backend object on the side that owns the context.
		m_handle = Rendering::Backend::GetHandleAllocator()
			.Allocate<Rendering::Backend::HwVertexArray>();

		Rendering::HAL::ResourceOps::EnsureVertexArray(m_handle);
	}

	VertexArrayObject::~VertexArrayObject()
	{
		Rendering::HAL::ResourceOps::DestroyVertexArray(m_handle);
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
			Rendering::HAL::ResourceOps::DestroyVertexArray(m_handle);

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
		Rendering::HAL::ResourceOps::SetVertexArrayLayout(
			m_handle, p_vertexBuffer, p_indexBuffer, p_attributes);

		m_attributeCount += static_cast<uint32_t>(p_attributes.size());
	}

	void VertexArrayObject::Bind() const
	{
		// Synchronous: the array is bound where the context is. The draw commands bind the array
		// they draw from themselves, so this is only for the immediate paths.
		if (Rendering::HAL::VertexArray* const array = Resolve(m_handle))
		{
			array->Bind();
		}
	}

	void VertexArrayObject::Unbind() const
	{
		if (Rendering::HAL::VertexArray* const array = Resolve(m_handle))
		{
			array->Unbind();
		}
	}

	uint32_t VertexArrayObject::GetID() const
	{
		return Rendering::Backend::Records::VertexArrayName(m_handle);
	}
}
