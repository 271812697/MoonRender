#pragma once
#include <QToolBar>
namespace MOON {
	class SketchToolbar : public QToolBar
	{
		Q_OBJECT
	public:
		SketchToolbar(const QString& title, QWidget* parent = nullptr);
		SketchToolbar(QWidget* parentObject = nullptr);
		~SketchToolbar()override;
		void disableAllHandlers();
		void setUncheckedAction(const std::string&name);
		/** Unchecks the external geometry button (the mode was left from elsewhere,
		 * e.g. Escape or the end of the sketch). */
		void uncheckExternalGeometry();
	private:
		class SketchToolbarInternal;
		SketchToolbarInternal* mInternal = nullptr;
		void constructor();
	};
}
