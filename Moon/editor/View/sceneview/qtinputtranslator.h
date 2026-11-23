#pragma once

#include <Rendering/View/InputEvent.h>

class QEvent;

namespace MOON::ViewportInput
{
	/** Translates a Qt event into the render module's own input event.
	 *
	 * This is the whole of the toolkit's involvement in input: the view, the camera
	 * controller and the input state only ever see keys, buttons, positions and wheel
	 * deltas (see InputEvent). Put another window framework under the module and only this
	 * function changes.
	 *
	 * @return false when the event carries nothing the module reads (it is not one of the
	 *         event types below, or it is a key or button the module has no use for).
	 */
	bool TranslateQtEvent(QEvent* p_event, ::Editor::Panels::InputEvent& p_out);
}
