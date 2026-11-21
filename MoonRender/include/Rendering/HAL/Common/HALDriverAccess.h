#pragma once

namespace Rendering::Context
{
	class Driver;
}

namespace Rendering::HAL
{
	/** The driver the module is running on, or null before one exists (a headless test, the
	 * start-up path before the module is built).
	 *
	 * HAL objects are created while assets load, which is on the recording side; with a render
	 * thread the context is not current there, so creation has to go through the driver. They ask
	 * for it here instead of reaching into the module themselves.
	 */
	::Rendering::Context::Driver* CurrentDriver();
}
