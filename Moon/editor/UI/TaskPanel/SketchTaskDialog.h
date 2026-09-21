#pragma once
#include "editor/UI/TaskPanel/ParamTaskDialog.h"
#include "editor/UI/TaskPanel/ShapeHelper.h"
class QListWidget;
class QLabel;
class QTimer;
class QEvent;
namespace MOON {
	class SketcherObj;
	class SketchTaskDialog : public ParamTaskDialog, public ShapeHelper
	{
		Q_OBJECT
	public:
		explicit SketchTaskDialog(QWidget* parent = nullptr,Feature* feature =nullptr);
		~SketchTaskDialog();
		virtual QVariant getParamValue(const QString& propertyName)override;
		virtual void setParamValue(const QString& propertyName, const QVariant& value)override;
		virtual void clickOk() override;
		virtual void clickApply() override;
		virtual void clickCancel() override;
		void onSelectPlane();
		void refreshLists();
		/** Writes what the solver makes of the sketch (degrees of freedom, conflicts,
		 * redundancies) into the status line of the panel. */
		void updateSolverStatus(SketcherObj* p_sketch);
		void syncCurveListSelection();
		bool eventFilter(QObject* watched, QEvent* event) override;
	private:
		QListWidget* mConstraintList = nullptr;
		QListWidget* mCurveList = nullptr;
		/** Geometry pulled in from other features: one row per reference, with the
		 * sub-shape it comes from and a button to drop it again. */
		QListWidget* mExternalList = nullptr;
		/** What the solver makes of the sketch: degrees of freedom and whether any
		 * constraint conflicts, is redundant or could not be understood. */
		QLabel* mSolverStatus = nullptr;
		QTimer* mRefreshTimer = nullptr;
		QString mListCache;
		QWidget* mCurveHoverRow = nullptr;
		class Internal;
		Internal* mInternal = nullptr;
	};
}
