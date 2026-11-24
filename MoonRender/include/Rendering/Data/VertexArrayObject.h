#pragma once
#include <cstdint>

#include <Rendering/Backend/ResourceHandle.h>
#include <Rendering/Settings/VertexAttribute.h>

namespace Rendering::Backend
{
	struct HwBufferObject;
	struct HwVertexArray;
}

namespace Rendering::Data
{
	/** The front end of a vertex array: what the recording side owns.
	 *
	 * It holds the handle that names the backend object - HAL::VertexArray, which lives in the
	 * handle's slot on the side that owns the context and is the only thing that has a GL name -
	 * plus the attribute count the recording side needs (whether the layout was applied). Every
	 * operation records the command that does it, or, with no driver at all, does the work inline;
	 * none of them touches GL on the recording side. The counterpart of Data::BufferObject.
	 */
	class VertexArrayObject
	{
	public:
		/** Reserves the handle and records the creation of its backend object. */
		VertexArrayObject();

		/** Frees the record now and queues the GL deletion to the side that owns the context. */
		~VertexArrayObject();

		VertexArrayObject(const VertexArrayObject&) = delete;
		VertexArrayObject& operator=(const VertexArrayObject&) = delete;
		VertexArrayObject(VertexArrayObject&& p_other) noexcept;
		VertexArrayObject& operator=(VertexArrayObject&& p_other) noexcept;

		/** Describes the layout the array keeps: the two buffers it reads and their attributes. */
		void SetLayout(
			const Rendering::Backend::ResourceHandle<Rendering::Backend::HwBufferObject>& p_vertexBuffer,
			const Rendering::Backend::ResourceHandle<Rendering::Backend::HwBufferObject>& p_indexBuffer,
			Settings::VertexAttributeLayout p_attributes);

		/** Binds the array. Synchronous: a bind is GL, so it only means something where the context
		 * is (the draw commands bind the array they draw from themselves). */
		void Bind() const;
		void Unbind() const;

		/** Whether a layout has been applied to this array. */
		bool IsValid() const { return m_attributeCount > 0; }

		/** The GL name, resolved on this side; 0 until the create command has run. */
		uint32_t GetID() const;

		const Rendering::Backend::ResourceHandle<Rendering::Backend::HwVertexArray>& GetHandle() const
		{
			return m_handle;
		}

	private:
		Rendering::Backend::ResourceHandle<Rendering::Backend::HwVertexArray> m_handle;
		/** How many attributes the layout described: the recording side's view of "layout set". */
		uint32_t m_attributeCount = 0;
	};
}
