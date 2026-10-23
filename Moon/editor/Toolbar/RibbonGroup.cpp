#include "editor/Toolbar/RibbonGroup.h"
#include "editor/Language.h"
#include "editor/Theme.h"
#include "editor/UiScale.h"

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
		// Metrics at UI scale 1.0. Every use multiplies them by UiScale(), together with the
		// application font, so the icons and the labels keep their ratio on any screen.
		constexpr int kButtonWidth = 58;
		constexpr int kButtonHeight = 50;
		constexpr int kIconSize = 23;
		constexpr int kLabelPointPx = 11;
		constexpr int kCaptionHeight = 14;

		// The colours are the theme's; only the metrics live here. Theme::RibbonButtonStyle()
		// builds the stylesheet, and Theme::Apply() rebuilds it when the scheme changes.
	}

	RibbonGroup::RibbonGroup(const QString& p_title, QWidget* p_parent)
		: QWidget(p_parent)
	{
		setObjectName("ribbonGroup");

		auto* layout = new QVBoxLayout(this);
		layout->setContentsMargins(std::lround(6 * UiScale()), std::lround(2 * UiScale()), std::lround(6 * UiScale()), std::lround(2 * UiScale()));
		layout->setSpacing(1);

		m_grid = new QGridLayout();
		m_grid->setContentsMargins(0, 0, 0, 0);
		m_grid->setHorizontalSpacing(1);
		m_grid->setVerticalSpacing(1);
		layout->addLayout(m_grid, 1);

		auto* caption = new QLabel(p_title, this);
		caption->setObjectName("ribbonGroupCaption");
		caption->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
		// The caption is a plain string; the language module looks its English spelling up in
		// its dictionary and rewrites the label when the language changes.
		caption->setProperty("moonZh", p_title);
		caption->setText(Language::Pick(p_title, Language::English(p_title)));
		QFont captionFont = caption->font();
		captionFont.setPointSizeF(std::max(7.0, captionFont.pointSizeF() - 1.5));
		captionFont.setLetterSpacing(QFont::AbsoluteSpacing, 0.4 * UiScale());
		caption->setFont(captionFont);
		// A caption labels the group; it stays quieter than the buttons beside it.
		caption->setProperty("moonCaption", true);
		caption->setStyleSheet(QString("color: %1;").arg(Theme::Tokens().textDim.name(QColor::HexRgb)));
		caption->setFixedHeight(std::lround(kCaptionHeight * UiScale()));
		layout->addWidget(caption, 0);

		setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
	}

	QToolButton* RibbonGroup::addAction(QAction* p_action)
	{
		const float scale = UiScale();
		auto* button = new QToolButton(this);
		button->setDefaultAction(p_action);
		button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
		const int iconSize = std::lround(kIconSize * scale);
		button->setIconSize(QSize(iconSize, iconSize));
		// Width comes from the content (icon above, label below) instead of a fixed column: a
		// one-character label like "点" does not need the room four characters do, and at a large
		// UI scale that difference is what keeps the sketch page's groups beside each other
		// rather than behind the toolbar's overflow button.
		button->setMinimumHeight(std::lround(kButtonHeight * scale));
		button->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
		button->setAutoRaise(true);
		button->setFocusPolicy(Qt::NoFocus);
		button->setCursor(Qt::PointingHandCursor);
		// The label size follows the UI scale, and the stylesheet (hover/checked plates
		// included) comes from the theme; the property lets Theme::Apply rebuild it when the
		// scheme changes.
		// English labels run wider than the two-to-four character Chinese ones, so they get a
		// slightly smaller size; that is what keeps the sketch page inside the window.
		const float labelScale = Language::Current() == Language::ELanguage::English ? 0.85f : 1.0f;
		const int labelPx = std::lround(kLabelPointPx * scale * labelScale);
		button->setProperty("moonLabelPx", labelPx);
		button->setStyleSheet(Theme::RibbonButtonStyle(labelPx));
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
			m_grid->setRowMinimumHeight(row, std::lround(kButtonHeight * UiScale()));
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
		painter.setPen(Theme::Tokens().separator);
		painter.drawLine(width() - 1, 4, width() - 1, height() - 5);
	}
}
