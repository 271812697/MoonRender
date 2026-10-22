#include "editor/UiScale.h"

#include <cstdlib>

namespace MOON
{
	float UiScale()
	{
		// The default is 1.5: at 1.0 the chrome reads small on a 1080p panel, and at 2.0 the
		// sketch page no longer fits beside the constraint groups.
		// MOON_UI_SCALE overrides it, which is how a screenshot at another density can be
		// taken without a rebuild.
		static const float scale = []
		{
			const char* p_override = std::getenv("MOON_UI_SCALE");
			const float value = p_override != nullptr ? static_cast<float>(std::atof(p_override)) : 1.5f;
			return value > 0.5f && value <= 4.0f ? value : 1.5f;
		}();
		return scale;
	}
}
