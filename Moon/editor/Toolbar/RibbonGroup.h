#pragma once
#include <QList>
#include <QWidget>

class QAction;
class QGridLayout;
class QToolButton;

namespace MOON
{
	/** One titled group of a ribbon toolbar.
	 *
	 * A ribbon is a row of these: buttons with the icon above the label, the group's
	 * name underneath, and a thin rule on the right that separates it from the next
	 * group. Twenty similar icons read as five themes instead of one strip.
	 *
	 * A button is bound to the action it is given (QToolButton::setDefaultAction), so
	 * a checkable command stays in sync with its action and the toolbar keeps no
	 * checked state of its own.
	 *
	 * A group can be laid out in more than one row (see setGrid): the sketch page puts
	 * the constraints beside the drawing tools, and two rows are what keeps that page
	 * within the width of a normal window. */
	class RibbonGroup : public QWidget
	{
		Q_OBJECT
	public:
		explicit RibbonGroup(const QString& p_title, QWidget* p_parent = nullptr);

		/** Adds one button for p_action. The button joins the grid at the next free
		 * cell; setGrid can be called before or after. */
		QToolButton* addAction(QAction* p_action);

		/** Lays the buttons out in at most p_columns columns, reserving p_rows rows.
		 *
		 * Reserved rows are kept even when they are empty, so every group on a page
		 * has the same height and its caption sits on the same line as the others'. */
		void setGrid(int p_columns, int p_rows);

		/** Whether to draw the vertical rule on the right edge. On by default: every
		 * group but the last one in a ribbon wants it. */
		void setDrawSeparator(bool p_draw);

	protected:
		virtual void paintEvent(QPaintEvent* p_event) override;

	private:
		void relayout();

		QGridLayout* m_grid = nullptr;
		QList<QToolButton*> m_buttons;
		int m_columns = 0; ///< 0: all buttons in a single row.
		int m_rows = 1;
		bool m_drawSeparator = true;
	};
}
