#pragma once
#include <QPoint>
#include <QWidget>

class QDockWidget;
class QLabel;
class QToolButton;

namespace MOON {

/** Inviwo-style dock widget title bar.
 *
 * Mirrors Inviwo's InviwoDockWidgetTitleBar (modules/qtwidgets): a header one step lighter
 * than the panel body, the title on the left and three glyph buttons on the right -
 * sticky (can this panel be docked at all), float/dock, and close. The glyphs are Inviwo's
 * 13x13 SVG shapes, drawn through Theme::Icon so they take the scheme's icon colour.
 *
 * A custom title bar widget turns off QDockWidget's own title-bar handling, so the window
 * interactions are implemented here as well: double click toggles floating, dragging a docked
 * bar undocks it and starts a system move, and the context menu offers float/dock and close. */
class DockWidgetTitleBar : public QWidget {
    Q_OBJECT
public:
    explicit DockWidgetTitleBar(QDockWidget* parent);
    ~DockWidgetTitleBar() override;

    // Inserts an extra widget after the title label and before the three buttons, so panels
    // can host small controls such as log level filters.
    void addTitleWidget(QWidget* widget, int stretch = 0);

    /** Inviwo's sticky flag: when off, every docking area is disabled, so the panel can only
     * stay floating. */
    void setSticky(bool sticky);
    bool isSticky() const;

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    void updateTitle();
    void refreshIcons();
    void applyDockAreas();
    void syncFloatButton();

    QDockWidget* dock_ = nullptr;
    QLabel* label_ = nullptr;
    QToolButton* stickyBtn_ = nullptr;
    QToolButton* floatBtn_ = nullptr;
    QToolButton* closeBtn_ = nullptr;
    /** The docking areas to restore when sticky is re-enabled. */
    Qt::DockWidgetAreas allowedDockAreas_ = Qt::AllDockWidgetAreas;
    bool updatingDockAreas_ = false;
    QPoint pressPos_;
    bool pressed_ = false;
    bool dragging_ = false;
};

}
