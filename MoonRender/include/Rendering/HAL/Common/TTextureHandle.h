#pragma once
#include <optional>

#include <Rendering/Settings/EGraphicsBackend.h>
#include <Rendering/Settings/ETextureType.h>

namespace Rendering::HAL
{
	/**
	* Represents a texture handle, acts as a view to the texture.
	*/
	template<Settings::EGraphicsBackend Backend, class Context>
	class TTextureHandle
	{
	public:
		/**
		* Binds the texture to the given slot.
		* @param p_slot Optional slot to bind the texture to.
		*/
		void Bind(std::optional<uint32_t> p_slot = std::nullopt) const;

		/**
		* Unbinds the texture.
		*/
		void Unbind() const;

		/**
		* Returns the ID associated with the texture.
		* @return The texture ID.
		*/
		uint32_t GetID() const;

		/** The handle that names the backend object (Filament's HwTexture handle), or 0 when this
		 * object has no handle of its own. */
		uint32_t GetHandleId() const;

		/** Whether this object carries a GL name of its own - i.e. it is the backend object, or
		 * there is no driver to record into and the name was made inline. False for the front end,
		 * whose operations are recorded and whose name only exists on the executor. */
		bool HasBackendName() const;

		/**
		* Returns the texture type
		*/
		Settings::ETextureType GetType() const;

	protected:
		TTextureHandle(Settings::ETextureType p_type);
		TTextureHandle(Settings::ETextureType p_type, uint32_t p_id);

	protected:
		Context m_context;
	};
}
