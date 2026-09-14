#pragma once
#include <QToolBar>
namespace MOON {


	class ViewerWindowTitleBar : public QToolBar
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
		/** Vertical, centered on the left edge of the parent, above its content. */
		void PlaceOverlay();

	private:
		class ViewerWindowTitleBarInternal;
		ViewerWindowTitleBarInternal* mInternal = nullptr;
	};
}
