#pragma once

namespace MOON
{
	/** Scale of the editor chrome: the application font and everything sized around it (the
	 * ribbon's icons, buttons and captions).
	 *
	 * One knob, because the two must not drift apart: a ribbon whose icons grew but whose labels
	 * did not looks broken, and so does the other way round. MOON_UI_SCALE overrides it, which is
	 * how a screenshot at another density can be taken without a rebuild. */
	float UiScale();
}
