#include <glad/glad.h>

#include <Rendering/Backend/BackendResources.h>
#include <Rendering/Context/Driver.h>
#include <Rendering/HAL/Common/HALDriverAccess.h>
#include <Rendering/HAL/Common/ResourceOps.h>
#include <Rendering/HAL/OpenGL/GLTypes.h>
#include <Rendering/HAL/OpenGL/GLVertexArray.h>
#include <assert.h>

template<>
Rendering::HAL::GLVertexArray::TVertexArray()
{
	// createVertexArrayS/createVertexArrayR: the handle exists here, the name is made on the side
	// that owns the context (a vertex array is a container object and is not shared).
	m_context.handle = ::Rendering::Backend::GetHandleAllocator().Allocate<::Rendering::Backend::HwVertexArray>();
	::Rendering::HAL::ResourceOps::EnsureVertexArray(m_context.handle);
}

template<>
Rendering::HAL::GLVertexArray::~TVertexArray()
{
	if (m_context.handle)
	{
		::Rendering::HAL::ResourceOps::DestroyVertexArray(m_context.handle);
		m_context.handle.clear();
		m_context.attributeCount = 0;
	}
}

template<>
bool Rendering::HAL::GLVertexArray::IsValid() const
{
	return m_context.attributeCount > 0;
}

template<>
void Rendering::HAL::GLVertexArray::SetLayout(
	Settings::VertexAttributeLayout p_attributes,
	VertexBuffer& p_vertexBuffer,
	IndexBuffer& p_indexBuffer
)
{
	assert(!IsValid()&& "Vertex array layout already set");

	// The layout is GL (glVertexAttribPointer against the array's buffers), so it goes through the
	// same handle-addressed path every other resource operation uses: recorded when the context
	// lives on another thread, applied here otherwise (see ResourceOps).
	::Rendering::HAL::ResourceOps::SetVertexArrayLayout(
		m_context.handle,
		p_vertexBuffer.GetHandle(),
		p_indexBuffer.IsValid() ? p_indexBuffer.GetHandle() : Rendering::Backend::ResourceHandle<Rendering::Backend::HwBufferObject>{},
		p_attributes);

	for (uint32_t index = 0; index < static_cast<uint32_t>(p_attributes.size()); ++index)
	{
		++m_context.attributeCount;
	}
}
template<>
void Rendering::HAL::GLVertexArray::ResetLayout()
{
	assert(IsValid()&&"Vertex array layout not already set");

	Bind();
	for (uint32_t i = 0; i < m_context.attributeCount; ++i)
	{
		glDisableVertexAttribArray(i);
	}
	m_context.attributeCount = 0;
	Unbind();
}

template<>
void Rendering::HAL::GLVertexArray::Bind() const
{
	glBindVertexArray(GetID());
}

template<>
void Rendering::HAL::GLVertexArray::Unbind() const
{
	glBindVertexArray(0);
}

template<>
uint32_t Rendering::HAL::GLVertexArray::GetID() const
{
	return ::Rendering::Backend::Records::VertexArrayName(m_context.handle);
}
