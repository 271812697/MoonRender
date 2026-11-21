#pragma once
#include <Rendering/Settings/EInternalFormat.h>
#include <Rendering/Settings/EGraphicsBackend.h>

namespace Rendering::HAL
{
	
	template<Settings::EGraphicsBackend Backend, class Context>
	class TRenderbuffer final
	{
	public:
		TRenderbuffer(bool multisample);
		~TRenderbuffer();
		void Bind() const;
		void Unbind() const;
		uint32_t GetID() const;
		/** The handle that names the backend object (Filament's HwRenderbuffer handle): the GL name
		 * lives in the record it names, so a framebuffer attach travels as this. */
		uint32_t GetHandleId() const;
		void Allocate(uint16_t p_width, uint16_t p_height, Settings::EInternalFormat p_format);
		bool IsValid() const;
		void Resize(uint16_t p_width, uint16_t p_height);
		uint16_t GetWidth() const;
		uint16_t GetHeight() const;

	private:
		bool isMultisample = false;
		Context m_context;
	};
}
