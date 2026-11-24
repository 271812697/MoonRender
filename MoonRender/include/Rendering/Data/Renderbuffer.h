#pragma once
#include <cstdint>

#include <Rendering/Backend/ResourceHandle.h>
#include <Rendering/Settings/EInternalFormat.h>

namespace Rendering::Backend
{
	struct HwRenderbuffer;
}

namespace Rendering::Data
{
	/** The front end of a renderbuffer: what the recording side owns (the depth/stencil storage of
	 * a render target's attachments).
	 *
	 * It holds the handle that names the backend object - HAL::Renderbuffer, which lives in the
	 * handle's slot on the side that owns the context and is the only thing that has a GL name -
	 * plus the size and format the recording side needs to report and to resize. Also the
	 * counterpart of Data::BufferObject.
	 */
	class Renderbuffer
	{
	public:
		/** Reserves the handle and records the creation of its backend object. */
		explicit Renderbuffer(bool p_multiSample);

		/** Frees the record now and queues the GL deletion to the side that owns the context. */
		~Renderbuffer();

		Renderbuffer(const Renderbuffer&) = delete;
		Renderbuffer& operator=(const Renderbuffer&) = delete;
		Renderbuffer(Renderbuffer&& p_other) noexcept;
		Renderbuffer& operator=(Renderbuffer&& p_other) noexcept;

		/** Specifies the storage; with a driver it is recorded (glNamedRenderbufferStorage is GL). */
		void Allocate(uint16_t p_width, uint16_t p_height, Settings::EInternalFormat p_format);

		/** Re-specifies the storage at a new size (the format stays the same). */
		void Resize(uint16_t p_width, uint16_t p_height);

		/** Binds the renderbuffer. Synchronous: only meaningful where the context is. */
		void Bind() const;

		/** Whether storage was specified. */
		bool IsValid() const { return m_allocated; }

		uint16_t GetWidth() const { return m_width; }
		uint16_t GetHeight() const { return m_height; }

		/** The GL name, resolved on this side; 0 until the create command has run. */
		uint32_t GetID() const;

		const Rendering::Backend::ResourceHandle<Rendering::Backend::HwRenderbuffer>& GetHandle() const
		{
			return m_handle;
		}

	private:
		Rendering::Backend::ResourceHandle<Rendering::Backend::HwRenderbuffer> m_handle;
		uint16_t m_width = 0;
		uint16_t m_height = 0;
		Settings::EInternalFormat m_format = Settings::EInternalFormat::RGBA;
		bool m_multiSample = false;
		bool m_allocated = false;
	};
}
