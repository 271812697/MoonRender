#pragma once
#include <functional>

namespace Rendering::Backend
{
	/** Work that has to run on the thread that owns the GL context, some time after whoever
	 * caused it is gone.
	 *
	 * A mesh is destroyed wherever its last reference is dropped - a loader, a feature rebuild,
	 * a job thread, closing a document - and the buffers it owns are GL objects: they have to be
	 * deleted with the module's context current. Deleting them at the point the reference is
	 * dropped is harmless today, when the frame runs on the same thread, and a use-after-free the
	 * moment the renderer has a thread of its own. So the destruction is queued here and run at
	 * the next drain of the command queue, which is by definition on that thread (and, today,
	 * right after the commands that may still have referenced them).
	 *
	 * It is deliberately a plain queue and not a command: what it carries is the *deletion* of a
	 * resource, and it must work even when no batch is being recorded.
	 */
	class DeferredDeleter
	{
	public:
		DeferredDeleter() = delete;

		/** Queues p_work to run at the next Flush(). Callable from any thread. */
		static void Push(std::function<void()> p_work);

		/** Says whether there is a module context to defer to at all. Before the render module
		 * exists - and after it is gone - resources are destroyed where they are released, which
		 * is what always happened. */
		static void SetActive(bool p_active);
		static bool IsActive();

		/** Whether the GPU side of a resource upload may be queued rather than run now.
		 *
		 * It may only once the frame runs on a thread of its own: until then the geometry an
		 * init-time draw needs (the gizmo is built and drawn inside the module build) has to be on
		 * the GPU before that draw, and running the queue at an arbitrary point in a single-threaded
		 * frame is exactly where the arrows went missing. With a render thread the draw that needs
		 * the data is itself queued, so the upload and the draw travel the same path.
		 */
		static void SetUploadsDeferrable(bool p_deferrable);
		static bool AreUploadsDeferrable();

		/** Runs everything queued, oldest first. Must be called with the module's context
		 * current: it is where the GL objects are deleted. */
		static void Flush();
	};
}
