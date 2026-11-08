#include "editor/UI/DockWidgetTitleBar.h"
#include "editor/Theme.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QCursor>
#include <QDockWidget>
#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QStyle>
#include <QStyleOption>
#include <QToolButton>
#include <QWindow>

#include <algorithm>

namespace MOON {

namespace {
    // Inviwo's title bar is 36 px tall at its 12 pt UI font, with 19 px glyphs and three
    // buttons packed together. The editor's base font is smaller (9 pt), so the sizes are
    // pinned here rather than derived from it: the header is meant to look the same.
    constexpr int kBarHeight = 36;
    constexpr int kTitlePx = 15;
    constexpr int kIconPx = 18;
    constexpr int kButtonSize = 28;

    QIcon twoStateIcon(const QString& onPath, const QString& offPath, int px) {
        QIcon icon = Theme::Icon(offPath);
        icon.addPixmap(Theme::Icon(onPath).pixmap(px), QIcon::Normal, QIcon::On);
        return icon;
    }
}

DockWidgetTitleBar::DockWidgetTitleBar(QDockWidget* parent)
    : QWidget(parent), dock_(parent) {
    setObjectName("DockWidgetTitleBar");
    setFixedHeight(kBarHeight);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    allowedDockAreas_ = parent->allowedAreas();

    label_ = new QLabel(parent->windowTitle(), this);
    label_->setObjectName("DockWidgetTitleBarLabel");
    label_->setMinimumWidth(120);  // Inviwo's 10 em: the title elides rather than forcing the dock wider
    auto labelPolicy = label_->sizePolicy();
    labelPolicy.setHorizontalPolicy(QSizePolicy::MinimumExpanding);
    labelPolicy.setRetainSizeWhenHidden(true);
    label_->setSizePolicy(labelPolicy);
    QFont titleFont = label_->font();
    titleFont.setPixelSize(kTitlePx);
    titleFont.setWeight(QFont::Normal);
    label_->setFont(titleFont);

    const auto makeButton = [this](const QString& tooltip) {
        auto* button = new QToolButton(this);
        button->setObjectName("DockTitleButton");
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::NoFocus);
        button->setFixedSize(kButtonSize, kButtonSize);
        button->setIconSize(QSize(kIconPx, kIconPx));
        button->setToolTip(tooltip);
        return button;
    };

    stickyBtn_ = makeButton(tr("Sticky (allow docking)"));
    stickyBtn_->setCheckable(true);
    stickyBtn_->setChecked(true);
    floatBtn_ = makeButton(tr("Float / Dock"));
    floatBtn_->setCheckable(true);
    floatBtn_->setChecked(parent->isFloating());
    closeBtn_ = makeButton(tr("Close"));

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 0, 6, 0);
    layout->setSpacing(0);
    layout->addWidget(label_, 1);
    layout->addWidget(stickyBtn_);
    layout->addWidget(floatBtn_);
    layout->addWidget(closeBtn_);

    // Inviwo's QSS, in this editor's palette: the header plate, the title padding, and buttons
    // whose only chrome is a hover border in the accent colour.
    setProperty("moonStyle", QStringLiteral(R"(
        QWidget#DockWidgetTitleBar {
            background: @header;
            border: none;
        }
        QLabel#DockWidgetTitleBarLabel {
            color: @text;
            background: transparent;
            padding-left: 2px;
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

    refreshIcons();

    connect(parent, &QDockWidget::windowTitleChanged, this,
            [this](const QString& title) {
                label_->setToolTip(title);
                updateTitle();
            });
    connect(parent, &QDockWidget::topLevelChanged, this, [this](bool) { syncFloatButton(); });
    connect(stickyBtn_, &QToolButton::toggled, this, [this](bool) { applyDockAreas(); });
    connect(floatBtn_, &QToolButton::clicked, this,
            [this]() {
                dock_->setFloating(!dock_->isFloating());
                syncFloatButton();
            });
    connect(closeBtn_, &QToolButton::clicked, dock_, &QDockWidget::close);
    connect(dock_, &QDockWidget::allowedAreasChanged, this, [this](Qt::DockWidgetAreas areas) {
        if (updatingDockAreas_) {
            return;
        }
        // Somebody else changed the areas while the panel is sticky: that is the set to
        // restore when sticky is turned back on.
        if (stickyBtn_->isChecked()) {
            allowedDockAreas_ = areas;
        }
    });

    label_->setToolTip(parent->windowTitle());
    applyDockAreas();
    syncFloatButton();
    updateTitle();
}

DockWidgetTitleBar::~DockWidgetTitleBar() = default;

void DockWidgetTitleBar::addTitleWidget(QWidget* widget, int stretch) {
    auto* layout = qobject_cast<QHBoxLayout*>(this->layout());
    if (!layout || !widget) {
        return;
    }

    // Before the three buttons, after the title.
    const int buttonsPos = std::max(0, layout->count() - 3);
    layout->insertSpacing(buttonsPos, 8);
    layout->insertWidget(buttonsPos + 1, widget, stretch);
    widget->show();
}

void DockWidgetTitleBar::setSticky(bool sticky) {
    QSignalBlocker blocker(stickyBtn_);
    stickyBtn_->setChecked(sticky);
    applyDockAreas();
}

bool DockWidgetTitleBar::isSticky() const {
    return stickyBtn_ != nullptr && stickyBtn_->isChecked();
}

void DockWidgetTitleBar::applyDockAreas() {
    if (dock_ == nullptr || stickyBtn_ == nullptr) {
        return;
    }
    updatingDockAreas_ = true;
    dock_->setAllowedAreas(isSticky() ? allowedDockAreas_ : Qt::NoDockWidgetArea);
    updatingDockAreas_ = false;
}

void DockWidgetTitleBar::refreshIcons() {
    if (stickyBtn_ == nullptr) {
        return;
    }
    // Inviwo swaps these per state: a filled dot means the panel may be docked, the outlined
    // pin means it is floating.
    stickyBtn_->setIcon(twoStateIcon(":/icons/dock/dock-sticky.svg",
        ":/icons/dock/dock-unsticky.svg", kIconPx));
    floatBtn_->setIcon(twoStateIcon(":/icons/dock/dock-floating.svg",
        ":/icons/dock/dock-docked.svg", kIconPx));
    closeBtn_->setIcon(Theme::Icon(":/icons/dock/close.svg"));
}

void DockWidgetTitleBar::paintEvent(QPaintEvent*) {
    // The plate comes from the stylesheet background; drawing the primitive is what lets a
    // plain QWidget honour it (the same trick Inviwo's title bar uses).
    QStyleOption option;
    option.initFrom(this);
    QPainter painter(this);
    style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);
}

void DockWidgetTitleBar::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updateTitle();
}

void DockWidgetTitleBar::changeEvent(QEvent* event) {
    // A scheme switch re-applies the application palette and stylesheet; the glyphs are
    // colourised SVG renders, so they have to be rebuilt too.
    if (event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange) {
        refreshIcons();
    }
    QWidget::changeEvent(event);
}

void DockWidgetTitleBar::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        pressPos_ = event->pos();
        pressed_ = true;
        dragging_ = false;
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void DockWidgetTitleBar::mouseMoveEvent(QMouseEvent* event) {
    if (!pressed_ || !(event->buttons() & Qt::LeftButton)) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    if (!dragging_ && (event->pos() - pressPos_).manhattanLength() >= QApplication::startDragDistance()) {
        dragging_ = true;
        if (!dock_->isFloating()) {
            // Undock, then hand the move to the window manager: a docked QDockWidget has no
            // title-bar drag of its own once a custom title bar widget is set.
            dock_->setFloating(true);
            dock_->move(QCursor::pos() - pressPos_);
        }
        if (QWindow* handle = dock_->windowHandle()) {
            handle->startSystemMove();
        } else if (dock_->winId() != 0) {
            if (QWindow* created = dock_->windowHandle()) {
                created->startSystemMove();
            }
        }
    }
    event->accept();
}

void DockWidgetTitleBar::mouseReleaseEvent(QMouseEvent* event) {
    pressed_ = false;
    dragging_ = false;
    QWidget::mouseReleaseEvent(event);
}

void DockWidgetTitleBar::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        dock_->setFloating(!dock_->isFloating());
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void DockWidgetTitleBar::contextMenuEvent(QContextMenuEvent* event) {
    QMenu menu(this);
    QAction* floatAction = menu.addAction(dock_->isFloating() ? tr("Dock") : tr("Float"));
    QAction* closeAction = menu.addAction(tr("Close"));
    QAction* chosen = menu.exec(event->globalPos());
    if (chosen == floatAction) {
        dock_->setFloating(!dock_->isFloating());
    } else if (chosen == closeAction) {
        dock_->close();
    }
}

void DockWidgetTitleBar::syncFloatButton() {
    if (floatBtn_ == nullptr || dock_ == nullptr) {
        return;
    }
    QSignalBlocker blocker(floatBtn_);
    floatBtn_->setChecked(dock_->isFloating());
}

void DockWidgetTitleBar::updateTitle() {
    if (dock_ == nullptr || label_ == nullptr) {
        return;
    }

    // Inviwo elides in the middle: the start of a panel title is what identifies it.
    const QString title = dock_->windowTitle();
    const int available = std::max(0, label_->width() - 4);
    QFontMetrics metrics(label_->font());
    const QString shown = available > 0
        ? metrics.elidedText(title, Qt::ElideMiddle, available)
        : title;
    if (label_->text() != shown) {
        label_->setText(shown);
    }
}

}
