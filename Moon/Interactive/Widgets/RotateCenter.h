#pragma once
#include "Interactive/EventWidget.h"
namespace MOON
{
	/** The rotate centre marker: drawn while the right mouse button is held.
	 *
	 * Selection used to live here too; it is in SelectWidget now, which is where a click in the
	 * viewport becomes a selection event.
	 */
	class RotateCenter : public EventWidget
	{
	public:
		RotateCenter(const std::string& name);
		virtual ~RotateCenter();
		virtual void onUpdate()override;
		virtual void onRightMousePressed()override;
		virtual void onRightMouseReleased()override;
	};
}
