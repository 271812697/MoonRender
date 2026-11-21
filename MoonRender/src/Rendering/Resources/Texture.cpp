#include <assert.h>
#include <Rendering/Resources/Texture.h>

#include <Rendering/Backend/BackendResources.h>
#include <Rendering/Backend/HandleAllocator.h>

Rendering::HAL::Texture& Rendering::Resources::Texture::GetTexture()
{
	Rendering::HAL::Texture* const texture =
		Rendering::Backend::GetHandleAllocator().HandleCast<Rendering::HAL::Texture>(m_handle);
	assert(texture != nullptr && "The backend object has not been created yet (see Backend::Records)");
	return *texture;
}

Rendering::Resources::Texture::Texture()
{
	// Front end only: the handle exists before the GPU object does (Filament's Texture allocates
	// its HwTexture handle before the driver creates the object).
	m_handle = Rendering::Backend::GetHandleAllocator().Allocate<Rendering::HAL::Texture>();
}

Rendering::Resources::Texture::Texture(const std::string p_path) : path(p_path)
{
	// Front end only: the handle exists before the GPU object does, and it is what the recorded
	// commands name (CreateTexture, UploadTexture, and the draws that sample it).
	m_handle = Rendering::Backend::GetHandleAllocator().Allocate<Rendering::HAL::Texture>();
}

Rendering::Resources::Texture::~Texture()
{
	if (m_handle)
	{
		// The allocator owns the object and queues its destruction to the context side.
		Rendering::Backend::Records::DestroyTexture(m_handle);
		m_handle.clear();
	}
}
