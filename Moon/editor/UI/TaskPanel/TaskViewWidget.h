#pragma once
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QScrollArea>
#include "BaseTaskDialog.h"
namespace MOON {
    class Feature;
    class TaskViewWidget : public QWidget
    {
        Q_OBJECT
    public:
        explicit TaskViewWidget(QWidget* parent = nullptr);

        // 切换任务UI
        void setTaskDialog(BaseTaskDialog* dlg);
        // 清空任务
        void clearTask();
        bool hasTask();
        /** The feature the panel that is open right now belongs to. Null when there
         * is no panel, or when the panel does not edit a feature (the sketch panel
         * works on its own sketch object). A caller that is about to delete features
         * asks for this: a panel holds its feature while it is open. */
        Feature* editingFeature() const;

    signals:
        void taskOk();
        void taskApply();
        void taskCancel();
    public slots:
        void clickOk();
        void clickApply();
        void clickCancel();
        void onSelectedFeature(void* feature);
    private:
        QVBoxLayout* m_mainLayout;
        QWidget* m_contentWidget;
        QVBoxLayout* m_contentLayout;
        BaseTaskDialog* m_currentTask = nullptr;
    };
}

