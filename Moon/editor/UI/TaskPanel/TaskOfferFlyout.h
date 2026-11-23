#pragma once
#include <vector>

#include <QString>
#include <QWidget>

class QEventLoop;
class QHideEvent;
class QKeyEvent;
class QPoint;
class QToolButton;

namespace MOON
{
	/** One button of the pick switcher: the icon an operation is shown with, and the name that
	 * says which one it is. */
	struct OfferChoice
	{
		/** Already localized - the switcher does not know what language this is. It is the
		 * button's tooltip: the bar itself is icons only. */
		QString name;
		/** A qrc path; it goes through Theme::Icon, so the SVG follows the colour scheme. */
		QString icon;
	};

	/** The question "which of these did you mean", asked at the cursor.
	 *
	 * A pick can offer more than one operation (an edge a fillet and a chamfer) and the geometry
	 * cannot say which one is meant. The answer is one click: every operation is a small button
	 * with the icon the ribbon shows for it, laid out in a row, and the one this user chose last
	 * time is the lit one. The name is the tooltip - the bar stays a row of pictograms.
	 *
	 * A QMenu cannot do that - it lists downwards and does not draw the icons - so this is a
	 * popup of its own. Qt's Popup flag brings the mouse grab and the click-outside dismissal a
	 * menu has; the wait for the answer is a nested event loop, the same way QMenu::exec() waits
	 * for its own.
	 */
	class TaskOfferFlyout : public QWidget
	{
	public:
		/** @param p_choices what to offer, in the order of the rule table.
		 * @param p_current the button that starts lit and focused, or -1 for the first one. */
		TaskOfferFlyout(std::vector<OfferChoice> p_choices, int p_current,
			QWidget* p_parent = nullptr);

		/** Shows it at the cursor and waits for the answer.
		 * @return the index that was clicked, -1 when the flyout was dismissed. */
		int run();

	protected:
		void keyPressEvent(QKeyEvent* p_event) override;
		void hideEvent(QHideEvent* p_event) override;

	private:
		void choose(int p_index);
		QToolButton* focusedButton() const;
		void moveFocus(int p_step);
		/** The corner it opens at: the cursor, kept inside the screen it was on. */
		QPoint besideCursor(const QPoint& p_cursor) const;

		std::vector<QToolButton*> m_buttons;
		/** The event loop run() is waiting in, so a dismissal can end it. */
		QEventLoop* m_loop = nullptr;
		int m_result = -1;
		int m_current = 0;
	};
}
