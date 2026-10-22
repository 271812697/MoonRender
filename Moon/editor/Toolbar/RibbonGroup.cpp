#include "editor/Toolbar/RibbonGroup.h"

#include <QAction>
#include <QGridLayout>
#include <QLabel>
#include <QPainter>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace MOON
{
	namespace
	{
		// A button has to be wide enough for its label: the widest labels here are four
		// Chinese characters ("加法管道"), about 48 px at the application font. 64 px
		// leaves a row readable; the two-row groups keep the ribbon within the window.
		constexpr int kButtonWidth = 64;
		constexpr int kButtonHeight = 54;
		constexpr int kIconSize = 22;

		// The rule between groups, and the hover/checked looks. The palette here is
		// DarkStyle's (window 53,53,53; highlight 42,130,218); the ribbon is drawn with
		// it rather than with a second theme of its own.
		const QColor kSeparatorColor(82, 82, 82);

		const char* const kButtonStyle = R"(
			QToolButton {
				color: #d8d8d8;
				background: transparent;
				border: 1px solid transparent;
				border-radius: 4px;
				padding: 1px;
			}
			QToolButton:hover {
				background: rgba(255, 255, 255, 0.07);
				border-color: #5c5c5c;
			}
			QToolButton:pressed {
				background: rgba(255, 255, 255, 0.12);
			}
			QToolButton:checked {
				background: rgba(42, 130, 218, 0.22);
				border-color: #2a82da;
			}
			QToolButton:disabled {
				color: #6f6f6f;
			}
		)";
	}

	RibbonGroup::RibbonGroup(const QString& p_title, QWidget* p_parent)
		: QWidget(p_parent)
	{
		setObjectName("ribbonGroup");

		auto* layout = new QVBoxLayout(this);
		layout->setContentsMargins(6, 2, 6, 2);
		layout->setSpacing(1);

		m_grid = new QGridLayout();
		m_grid->setContentsMargins(0, 0, 0, 0);
		m_grid->setHorizontalSpacing(1);
		m_grid->setVerticalSpacing(1);
		layout->addLayout(m_grid, 1);

		auto* caption = new QLabel(p_title, this);
		caption->setObjectName("ribbonGroupCaption");
		caption->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
		QFont captionFont = caption->font();
		captionFont.setPointSizeF(std::max(7.0, captionFont.pointSizeF() - 1.0));
		caption->setFont(captionFont);
		QPalette captionPalette = caption->palette();
		captionPalette.setColor(QPalette::WindowText,
			captionPalette.color(QPalette::Disabled, QPalette::WindowText));
		caption->setPalette(captionPalette);
		layout->addWidget(caption, 0);

		setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
	}

	QToolButton* RibbonGroup::addAction(QAction* p_action)
	{
		auto* button = new QToolButton(this);
		button->setDefaultAction(p_action);
		button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
		button->setIconSize(QSize(kIconSize, kIconSize));
		button->setFixedSize(kButtonWidth, kButtonHeight);
		button->setAutoRaise(true);
		button->setFocusPolicy(Qt::NoFocus);
		button->setCursor(Qt::PointingHandCursor);
		button->setStyleSheet(kButtonStyle);
		m_buttons.append(button);
		relayout();
		return button;
	}

	void RibbonGroup::setGrid(int p_columns, int p_rows)
	{
		m_columns = std::max(1, p_columns);
		m_rows = std::max(1, p_rows);
		relayout();
	}

	void RibbonGroup::setDrawSeparator(bool p_draw)
	{
		m_drawSeparator = p_draw;
		update();
	}

	void RibbonGroup::relayout()
	{
		while (QLayoutItem* item = m_grid->takeAt(0))
		{
			// The grid owns the items, not the buttons: dropping the item leaves the
			// widget alive for the next addWidget below.
			delete item;
		}

		const int columns = m_columns > 0
			? m_columns
			: std::max(1, static_cast<int>(m_buttons.size()));
		for (int index = 0; index < m_buttons.size(); ++index)
		{
			m_grid->addWidget(m_buttons[index], index / columns, index % columns);
		}

		// Empty reserved rows still take a button's height, so a group with a single
		// row of buttons keeps the caption of the two-row groups beside it in line.
		for (int row = 0; row < m_rows; ++row)
		{
			m_grid->setRowMinimumHeight(row, kButtonHeight);
		}
	}

	void RibbonGroup::paintEvent(QPaintEvent* p_event)
	{
		QWidget::paintEvent(p_event);
		if (!m_drawSeparator)
		{
			return;
		}

		QPainter painter(this);
		painter.setPen(kSeparatorColor);
		painter.drawLine(width() - 1, 4, width() - 1, height() - 5);
	}
}
