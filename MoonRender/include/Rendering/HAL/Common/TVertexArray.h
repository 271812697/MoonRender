#pragma once
#include <cstdint>
#include <vector>

#include <Rendering/Backend/BackendResources.h>
#include <Rendering/Backend/VertexArrayRequests.h>
#include <Rendering/HAL/Common/BackendConstructionTag.h>
#include <Rendering/Settings/EGraphicsBackend.h>

namespace Rendering::HAL
{
	/**
	* Represents a vertex array, used to descript the vertex layout to the graphics backend.
	*
	* This is the *backend* object, the counterpart of TBuffer for vertex arrays: it is constructed
	* in the slot a handle names (see Backend::Records::CreateVertexArray), it owns the GL name, and
	* its methods are the GL operations the executor runs. What the recording side holds instead is
	* a handle, wrapped by Rendering::Data::VertexArrayObject, which only records commands.
	*/
	template<Settings::EGraphicsBackend Backend, class VertexArrayContext>
	class TVertexArray : public Rendering::Backend::HwVertexArray
	{
	public:
		/**
		* Creates the backend object: the GL name is made here, so this runs where the context is
		* (the executor of a create-vertex-array command). The recording side never runs it.
		*/
		TVertexArray(BackendConstructionTag);

		/** Does not touch GL: the name is deleted by Backend::Records::DestroyVertexArray, which
		 * queues it to the side that owns the context. */
		~TVertexArray() = default;

		/** Binds the array. */
		void Bind() const;

		/** Binds nothing (the array is left unbound). */
		void Unbind() const;

		/** Binds the array and its two buffers and describes every attribute (the body of
		 * glVertexAttribPointer a layout ends up as). The buffers are named, not owned: 0 means
		 * "no index buffer". */
		void SetLayout(
			const std::vector<Rendering::Backend::VertexLayoutAttribute>& p_attributes,
			uint32_t p_vertexBufferId,
			uint32_t p_indexBufferId) const;

		/** Returns the GL name of the array. */
		uint32_t GetID() const;

	protected:
		VertexArrayContext m_context;
	};
}
