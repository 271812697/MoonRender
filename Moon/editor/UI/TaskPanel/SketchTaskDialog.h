#pragma once
#include "editor/UI/TaskPanel/ParamTaskDialog.h"
#include "editor/UI/TaskPanel/ShapeHelper.h"
class QListWidget;
class QTimer;
class QEvent;
namespace MOON {
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
		void syncCurveListSelection();
		bool eventFilter(QObject* watched, QEvent* event) override;
	private:
		QListWidget* mConstraintList = nullptr;
		QListWidget* mCurveList = nullptr;
		/** Geometry pulled in from other features: one row per reference, with the
		 * sub-shape it comes from and a button to drop it again. */
		QListWidget* mExternalList = nullptr;
		QTimer* mRefreshTimer = nullptr;
		QString mListCache;
		QWidget* mCurveHoverRow = nullptr;
		class Internal;
		Internal* mInternal = nullptr;
	};
}
