#pragma once
#include <gp_Pln.hxx>

namespace MOON
{
	class PadTaskWidget;

	/** The pad's interactive handles on their own.
	 *
	 * They used to be built inside the extrude panel, which made the panel the only thing that
	 * could put a pad's handles on screen. But they are the viewport half of the operation - where
	 * it stands, which way it grows - and they are needed before a panel exists at all: a face is
	 * picked, the handles appear, and dragging one is what starts the task. Keeping them in a
	 * object of their own lets the panel and whoever offers the operation on a pick share the very
	 * same handles, so nothing has to be rebuilt (and nothing jumps) when the task opens.
	 *
	 * The set owns the two widgets: whoever holds the set holds the handles.
	 */
	class PadHandleSet
	{
	public:
		PadHandleSet();
		~PadHandleSet();

		PadHandleSet(const PadHandleSet&) = delete;
		PadHandleSet& operator=(const PadHandleSet&) = delete;

		/** Places both handles on the plane: the origin of the operation and the two directions it
		 * can grow in. Their lengths are left to the caller - a handle that is offering an
		 * operation shows whatever its parameter already is. */
		void Attach(const gp_Pln& p_plane, bool p_additive);

		/** Shown or hidden as a whole. Hidden handles take no input and draw nothing. */
		void SetActive(bool p_active);

		/** The handle along the direction the material is added in. */
		PadTaskWidget* Forward() const { return m_forward; }
		/** The one along the opposite direction. */
		PadTaskWidget* Reverse() const { return m_reverse; }

	private:
		PadTaskWidget* m_forward = nullptr;
		PadTaskWidget* m_reverse = nullptr;
	};
}
