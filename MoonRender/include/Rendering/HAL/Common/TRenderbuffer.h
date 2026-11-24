#pragma once
#include <cstdint>

#include <Rendering/Backend/BackendResources.h>
#include <Rendering/HAL/Common/BackendConstructionTag.h>
#include <Rendering/Settings/EInternalFormat.h>
#include <Rendering/Settings/EGraphicsBackend.h>

namespace Rendering::HAL
{
	/**
	* Represents a renderbuffer, the depth/stencil storage of a render target.
	*
	* This is the *backend* object, like TBuffer and TVertexArray: it is constructed in the slot a
	* handle names (see Backend::Records::CreateRenderbuffer), it owns the GL name, and its methods
	* are the GL operations the executor runs. The recording side holds a handle, wrapped by
	* Rendering::Data::Renderbuffer, which only records commands.
	*/
	template<Settings::EGraphicsBackend Backend, class Context>
	class TRenderbuffer : public Rendering::Backend::HwRenderbuffer
	{
	public:
		/**
		* Creates the backend object: the GL name is made here, so this runs where the context is.
		*/
		TRenderbuffer(BackendConstructionTag);

		/** Does not touch GL: the name is deleted by Backend::Records::DestroyRenderbuffer. */
		~TRenderbuffer() = default;

		/** Binds the renderbuffer (some drivers want it current before it is attached). */
		void Bind() const;

		/** glNamedRenderbufferStorage{,Multisample}: (re-)specifying the storage is GL, and it is
		 * also what a resize is. */
		void Allocate(
			uint16_t p_width,
			uint16_t p_height,
			Settings::EInternalFormat p_format,
			bool p_multiSample) const;

		/** Returns the GL name of the renderbuffer. */
		uint32_t GetID() const;

	protected:
		Context m_context;
	};
}
