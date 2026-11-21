#pragma once

namespace Rendering::Context
{
	class Driver;
}

namespace Rendering::Context
{
	/** What the engine needs from whoever owns the window system: a driver - and with it a GL
	 * context - created on the thread the engine is running on.
	 *
	 * This is Filament's Platform (see backend/include/backend/Platform.h: `createDriver(void*
	 * sharedContext, const DriverConfig&)`), reduced to what we need today. The point of the
	 * interface is the threading contract: Qt pins a QOpenGLContext to the thread that created it,
	 * so the context cannot be made current anywhere else. The engine therefore asks the platform
	 * to create the driver *inside its own render thread*, and every GL object the frame uses is
	 * created there from then on.
	 *
	 * p_sharedContext is opaque to the engine: for the editor it is the widget's QOpenGLContext*
	 * that the module's context joins the share group of. Ownership of the returned driver stays
	 * with the caller (the engine); DestroyDriver is where the platform releases what it created
	 * around it.
	 */
	class IPlatform
	{
	public:
		virtual ~IPlatform() = default;

		/** Creates the driver on the calling thread. Returns null when the context could not be
		 * created or made current; the caller then reports the failure and builds nothing. */
		virtual Driver* CreateDriver(void* p_sharedContext) = 0;

		/** Releases what CreateDriver created. Called on the same thread, before the driver is
		 * destroyed. */
		virtual void DestroyDriver(Driver* p_driver) = 0;
	};
}
