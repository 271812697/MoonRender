#include <glad/glad.h>
#include <Rendering/Backend/BackendResources.h>
#include <Rendering/HAL/OpenGL/GLTextureHandle.h>
#include <Rendering/HAL/OpenGL/GLTypes.h>

template<>
Rendering::HAL::GLTextureHandle::TTextureHandle(Settings::ETextureType p_type) : m_context{
	.type = EnumToValue<GLenum>(p_type)
}
{
}

template<>
Rendering::HAL::GLTextureHandle::TTextureHandle(Settings::ETextureType p_type, uint32_t p_id) : m_context{
	.id = p_id,
	.type = EnumToValue<GLenum>(p_type)
}
{
}

template<>
void Rendering::HAL::GLTextureHandle::Bind(std::optional<uint32_t> p_slot) const
{
	// Binding is GL, so it only means something where the name is: inside a command, or when there
	// is no driver to record into at all. The recording side records BindDescriptor instead (see
	// Descriptor::Bind and MaterialInstance::BindParameterBlock).
	const uint32_t id = GetID();
	if (p_slot.has_value())
	{
		glBindTextureUnit(p_slot.value(), id);
	}
	else
	{
		glBindTexture(m_context.type, id);
	}
}

template<>
void Rendering::HAL::GLTextureHandle::Unbind() const
{
	glBindTexture(m_context.type, 0);
}

template<>
uint32_t Rendering::HAL::GLTextureHandle::GetID() const
{
	if (m_context.id != 0u)
	{
		return m_context.id;
	}
	// Front end: the name is in the record the handle names.
	return ::Rendering::Backend::Records::TextureName(m_context.handleId);
}

template<>
uint32_t Rendering::HAL::GLTextureHandle::GetHandleId() const
{
	return m_context.handleId;
}

template<>
bool Rendering::HAL::GLTextureHandle::HasBackendName() const
{
	return m_context.id != 0u;
}

template<>
Rendering::Settings::ETextureType Rendering::HAL::GLTextureHandle::GetType() const
{
	return ValueToEnum<Settings::ETextureType>(m_context.type);
}
