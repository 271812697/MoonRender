#include "editor/UI/DockWidgetTitleBar.h"
#include "editor/Theme.h"

#include <QDockWidget>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QToolButton>

#include <algorithm>

namespace MOON {

DockWidgetTitleBar::DockWidgetTitleBar(QDockWidget* parent)
    : QWidget(parent), dock_(parent) {
    setObjectName("DockWidgetTitleBar");

    label_ = new QLabel(parent->windowTitle(), this);
    label_->setObjectName("DockWidgetTitleBarLabel");
    auto labelPolicy = label_->sizePolicy();
    labelPolicy.setHorizontalPolicy(QSizePolicy::MinimumExpanding);
    label_->setSizePolicy(labelPolicy);

    floatBtn_ = new QToolButton(this);
    floatBtn_->setObjectName("DockTitleButton");
    floatBtn_->setCheckable(true);
    floatBtn_->setChecked(parent->isFloating());
    floatBtn_->setFocusPolicy(Qt::NoFocus);

    closeBtn_ = new QToolButton(this);
    closeBtn_->setObjectName("DockTitleButton");
    closeBtn_->setFocusPolicy(Qt::NoFocus);

    const QSize iconSize(16, 16);
    QIcon floatIcon;
    floatIcon.addFile(":/darkstyle/icon_restore.png", iconSize, QIcon::Normal, QIcon::Off);
    floatIcon.addFile(":/darkstyle/icon_undock.png", iconSize, QIcon::Normal, QIcon::On);
    floatBtn_->setIcon(floatIcon);
    floatBtn_->setIconSize(iconSize);
    closeBtn_->setIcon(QIcon(":/darkstyle/icon_close.png"));
    closeBtn_->setIconSize(iconSize);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 0, 2, 0);
    layout->setSpacing(0);
    layout->addWidget(label_, 0);
    // Absorbs the remaining width so optional title widgets stay on the
    // left while the float/close buttons stay pinned to the right.
    layout->addStretch(1);
    layout->addWidget(floatBtn_);
    layout->addWidget(closeBtn_);

    // Colours come from the theme (see Theme::Apply, which re-evaluates this template when the
    // scheme changes).
    setProperty("moonStyle", QStringLiteral(R"(
        QWidget#DockWidgetTitleBar {
            background: @border;
            border: none;
        }
        QLabel#DockWidgetTitleBarLabel {
            color: @text;
            background: transparent;
            font-weight: 400;
        }
        QToolButton#DockTitleButton {
            background: transparent;
            border: 1px solid transparent;
            padding: 2px;
        }
        QToolButton#DockTitleButton:hover {
            border: 1px solid @accent;
        }
    )"));
    setStyleSheet(MOON::Theme::Style(property("moonStyle").toString()));

    connect(parent, &QDockWidget::windowTitleChanged, this,
            [this](const QString&) { updateTitle(); });
    connect(parent, &QDockWidget::topLevelChanged, floatBtn_, &QToolButton::setChecked);
    connect(floatBtn_, &QToolButton::clicked, this, [this]() {
        dock_->setFloating(!dock_->isFloating());
    });
    connect(closeBtn_, &QToolButton::clicked, dock_, &QDockWidget::close);

    updateTitle();
}

DockWidgetTitleBar::~DockWidgetTitleBar() = default;

void DockWidgetTitleBar::addTitleWidget(QWidget* widget, int stretch)
{
    auto* layout = qobject_cast<QHBoxLayout*>(this->layout());
    if (!layout || !widget) {
        return;
    }

    // Insert into the left group, right before the expanding spacer, so the
    // widgets follow the title label and keep the order they were added in.
    int spacerPos = -1;
    for (int i = 0; i < layout->count(); ++i) {
        if (auto* spacer = layout->itemAt(i)->spacerItem()) {
            // Only the expanding spacer separates the left title group from
            // the float/close buttons; the 8px gaps are fixed spacers.
            if (spacer->sizePolicy().horizontalPolicy() == QSizePolicy::Expanding) {
                spacerPos = i;
                break;
            }
        }
    }
    if (spacerPos < 0) {
        // Fallback for layouts without a spacer: insert before float/close.
        spacerPos = layout->count() - 2;
    }

    layout->insertSpacing(spacerPos, 8);
    layout->insertWidget(spacerPos + 1, widget, stretch);
    widget->show();
}

void DockWidgetTitleBar::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updateTitle();
}

void DockWidgetTitleBar::updateTitle() {
    const QString title = dock_->windowTitle();
    QFontMetrics fm(label_->font());
    // Keep at least the full title width so a label that is not expanded by
    // the layout (title widgets take the left, a spacer the remaining space)
    // never collapses to an empty line.
    label_->setMinimumWidth(fm.horizontalAdvance(title) + 4);
    label_->setText(fm.elidedText(title, Qt::ElideMiddle,
                                  std::max(0, label_->width() - 4)));
}

}
