#pragma once
#include "Interactive/EventWidget.h"

namespace MOON
{
	/** Selection: the one place where a click in the viewport becomes a selection event.
	 *
	 * Picking itself publishes nothing. The picking pass draws an id per pixel and the result
	 * under the cursor is read back on demand - it answers queries, it has no events. A widget is
	 * what turns that answer into an event, and selection is this one: it reads the pick, tells
	 * SelectionManager, and that is what the task panels, the tree and the tools listen to.
	 *
	 * It used to live in RotateCenter, which draws a marker and had no business owning the
	 * selection of the whole viewport.
	 */
	class SelectWidget : public EventWidget
	{
	public:
		SelectWidget(const std::string& name);
		virtual ~SelectWidget();
		/** Publishes the pick under the cursor as the selection - or clears the selection when
		 * the click landed on nothing. */
		virtual void onLeftMousePressed() override;
		/** Tracks the corners of a selection rectangle. The rectangle pick itself is not
		 * implemented yet; the corners are kept here because they are this widget's state. */
		virtual void onLeftMouseReleased() override;
		/** The pick under the cursor is the preselection: what hovering highlights. */
		virtual void onMouseMove() override;
		/** Ctrl turns a click into an addition to the selection instead of a replacement. */
		virtual void onKeyPress(const std::string& key) override;
		virtual void onKeyRelease(const std::string& key) override;

	private:
		void publishPick();
		void updatePreselect();

		float m_rectStartX = 0.0f;
		float m_rectStartY = 0.0f;
		float m_rectEndX = 0.0f;
		float m_rectEndY = 0.0f;
	};
}
