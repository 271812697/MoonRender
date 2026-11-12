#include "editor/UI/TaskPanel/TaskOfferFlyout.h"

#include <algorithm>
#include <cmath>

#include <QCursor>
#include <QEventLoop>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QKeyEvent>
#include <QPoint>
#include <QScreen>
#include <QToolButton>
#include <QVBoxLayout>

#include "editor/Theme.h"
#include "editor/UiScale.h"

namespace MOON
{
	TaskOfferFlyout::TaskOfferFlyout(std::vector<OfferChoice> p_choices, int p_current,
		QWidget* p_parent)
		: QWidget(p_parent, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint)
	{
		setObjectName(QStringLiteral("OfferFlyout"));
		// The plate is a widget of its own rather than this window's background: a translucent
		// top-level window does not paint a stylesheet background, and the rounded corners need
		// the window itself to be translucent.
		setAttribute(Qt::WA_TranslucentBackground, true);
		setStyleSheet(Theme::Style(QStringLiteral(R"(
			#OfferPlate {
				background: @surface;
				border: 1px solid @border;
				border-radius: 8px;
			}
		)")));

		auto* plate = new QWidget(this);
		plate->setObjectName(QStringLiteral("OfferPlate"));
		// A plain QWidget paints no background for a stylesheet unless it is told to.
		plate->setAttribute(Qt::WA_StyledBackground, true);
		auto* outer = new QVBoxLayout(this);
		outer->setContentsMargins(0, 0, 0, 0);
		outer->addWidget(plate);

		const float scale = UiScale();
		// Icon only: the popup is a small bar of pictograms, and the name an icon cannot carry
		// is the tooltip. 16 points is the small-icon step the rest of the chrome uses, against
		// the 23 the ribbon gives its buttons.
		const int iconPx = std::lround(16 * scale);
		const int padding = std::lround(4 * scale);

		auto* row = new QHBoxLayout(plate);
		const int margin = std::lround(5 * scale);
		row->setContentsMargins(margin, margin, margin, margin);
		row->setSpacing(std::lround(3 * scale));
		for (const OfferChoice& choice : p_choices)
		{
			auto* button = new QToolButton(this);
			button->setToolButtonStyle(Qt::ToolButtonIconOnly);
			button->setIconSize(QSize(iconPx, iconPx));
			button->setIcon(Theme::Icon(choice.icon));
			button->setToolTip(choice.name);
			button->setAccessibleName(choice.name);
			// A fixed box, so the two icons do not come out different sizes and the whole bar
			// stays the same width whatever the pixmap inside it is.
			button->setFixedSize(iconPx + 2 * padding, iconPx + 2 * padding);
			button->setCheckable(true);
			button->setFocusPolicy(Qt::StrongFocus);
			button->setCursor(Qt::PointingHandCursor);
			button->setStyleSheet(Theme::RibbonButtonStyle(11)
				+ Theme::Style(QStringLiteral("QToolButton:focus { border-color: @accent; }")));
			const int index = static_cast<int>(m_buttons.size());
			connect(button, &QToolButton::clicked, this, [this, index]() { choose(index); });
			row->addWidget(button);
			m_buttons.push_back(button);
		}

		m_current = p_current >= 0 && p_current < static_cast<int>(m_buttons.size()) ? p_current : 0;
		if (!m_buttons.empty())
		{
			m_buttons[static_cast<size_t>(m_current)]->setChecked(true);
		}
	}

	int TaskOfferFlyout::run()
	{
		adjustSize();
		move(besideCursor(QCursor::pos()));
		// The loop has to be in place before the window is shown: a click can dismiss a popup the
		// moment Qt's grab takes it, and that dismissal has to end a loop that is running.
		QEventLoop loop;
		m_loop = &loop;
		show();
		raise();
		if (!m_buttons.empty())
		{
			m_buttons[static_cast<size_t>(m_current)]->setFocus(Qt::OtherFocusReason);
		}
		if (isVisible())
		{
			loop.exec();
		}
		m_loop = nullptr;
		hide();
		return m_result;
	}

	void TaskOfferFlyout::keyPressEvent(QKeyEvent* p_event)
	{
		switch (p_event->key())
		{
		case Qt::Key_Escape:
			close();
			return;
		case Qt::Key_Left:
		case Qt::Key_Up:
			moveFocus(-1);
			return;
		case Qt::Key_Right:
		case Qt::Key_Down:
			moveFocus(1);
			return;
		case Qt::Key_Return:
		case Qt::Key_Enter:
			if (QToolButton* button = focusedButton())
			{
				button->click();
			}
			return;
		default:
			break;
		}
		QWidget::keyPressEvent(p_event);
	}

	void TaskOfferFlyout::hideEvent(QHideEvent* p_event)
	{
		// Dismissed: a click outside (Qt closes popup windows by itself), Escape, or a button
		// that was pressed. The question is over either way.
		if (m_loop != nullptr)
		{
			m_loop->quit();
		}
		QWidget::hideEvent(p_event);
	}

	void TaskOfferFlyout::choose(int p_index)
	{
		m_result = p_index;
		close();
	}

	QToolButton* TaskOfferFlyout::focusedButton() const
	{
		for (QToolButton* button : m_buttons)
		{
			if (button->hasFocus())
			{
				return button;
			}
		}
		return nullptr;
	}

	void TaskOfferFlyout::moveFocus(int p_step)
	{
		const int count = static_cast<int>(m_buttons.size());
		if (count == 0)
		{
			return;
		}
		int current = m_current;
		if (QToolButton* focused = focusedButton())
		{
			// The keyboard follows the button that has the focus, which is not necessarily the
			// lit one once the mouse has been over the flyout.
			current = static_cast<int>(
				std::find(m_buttons.begin(), m_buttons.end(), focused) - m_buttons.begin());
		}
		m_current = ((current + p_step) % count + count) % count;
		m_buttons[static_cast<size_t>(m_current)]->setFocus(Qt::OtherFocusReason);
	}

	QPoint TaskOfferFlyout::besideCursor(const QPoint& p_cursor) const
	{
		QScreen* screen = QGuiApplication::screenAt(p_cursor);
		if (screen == nullptr)
		{
			screen = QGuiApplication::primaryScreen();
		}
		QPoint pos = p_cursor;
		if (screen != nullptr)
		{
			const QRect available = screen->availableGeometry();
			pos.setX(std::clamp(pos.x(), available.left(),
				std::max(available.left(), available.right() - width() + 1)));
			pos.setY(std::clamp(pos.y(), available.top(),
				std::max(available.top(), available.bottom() - height() + 1)));
		}
		return pos;
	}
}
