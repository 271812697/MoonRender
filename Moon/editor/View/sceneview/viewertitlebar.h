#pragma once
#include <QWidget>

namespace MOON {


	/** The view overlay: a small floating card in the top left corner of the
	 * viewport, the categories side by side like a menu bar. Clicking one drops
	 * that category's buttons below it, the way a menu bar drops a menu.
	 *
	 * Grouping exists because a flat strip runs out of room as soon as there are a
	 * few buttons. Adding a button is one line in ViewerWindowTitleBarInternal (add
	 * it to the category it belongs to); adding a category is one more AddCategory()
	 * call, see viewertitlebar.cpp.
	 */
	class ViewerWindowTitleBar : public QWidget
	{
	public:
		explicit ViewerWindowTitleBar(QWidget* parent = nullptr);
		~ViewerWindowTitleBar();
	protected:
		/** Keeps the bar on the left edge of the viewport it floats over, whenever
		 * that viewport is resized or shown. */
		bool eventFilter(QObject* p_watched, QEvent* p_event) override;
		/** The bar's own size is only final after the layout (and the style) ran,
		 * and it changes with the icon size: recenter whenever it does. */
		void resizeEvent(QResizeEvent* p_event) override;
		void showEvent(QShowEvent* p_event) override;
	private:
		/** Pinned to the top left corner of the parent, above its content. */
		void PlaceOverlay();

	private:
		class ViewerWindowTitleBarInternal;
		ViewerWindowTitleBarInternal* mInternal = nullptr;
	};
}
