#include "editor/UI/HomePanel/HomePanel.h"

#include <functional>
#include <algorithm>
#include <vector>

#include <QDateTime>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QToolButton>
#include <QVariantAnimation>
#include <QVBoxLayout>

#include "Core/Global/ServiceLocator.h"
#include "core/log.h"
#include "editor/UI/HomePanel/RecentProjects.h"

namespace
{
	/** The size of a card on the start page: fixed, so the flow layout has something
	 * to lay out - and the picture inside it, which is 16:9 like the viewport. */
	constexpr int kCardWidth = 480;
	constexpr int kCardHeight = 392;
	constexpr int kCardPictureWidth = 448;
	constexpr int kCardPictureHeight = 252;
	/** The card's inset, the size of the button in its corner, and what the hover
	 * animation moves: the texts travel up this far, and the picture grows this much. */
	constexpr int kCardInset = 16;
	constexpr int kForgetButtonSize = 28;
	constexpr int kCardHoverLift = 60;
	constexpr int kCardHoverZoom = 12;
	/** How much larger than the application's font the name, and the line under it,
	 * are - a card this size carries text that would look lost at the base size. */
	constexpr qreal kCardNameFontDelta = 3.0;
	constexpr qreal kCardWhenFontDelta = 1.0;

	/** Lays its items out left to right and wraps to a new row when the width runs
	 * out - the shape a page of cards wants, as opposed to a grid, which would stretch
	 * whatever is in a column to fill it. (The flow layout from the Qt examples, kept
	 * here because the start page is the only place that needs one.) */
	class FlowLayout : public QLayout
	{
	public:
		explicit FlowLayout(QWidget* p_parent, int p_spacing = 12)
			: QLayout(p_parent)
			, m_space(p_spacing)
		{
			setContentsMargins(0, 0, 0, 0);
		}

		~FlowLayout() override
		{
			// Drain the list itself: takeAt() on an empty list is out of range, which is
			// undefined for a QList and reads past the end of it - and this runs while
			// the application is going down, with a window that has no items left.
			while (!m_items.isEmpty()) {
				delete m_items.takeFirst();
			}
		}

		void addItem(QLayoutItem* p_item) override { m_items.append(p_item); }
		int count() const override { return m_items.size(); }
		QLayoutItem* itemAt(int p_index) const override { return m_items.value(p_index); }
		QLayoutItem* takeAt(int p_index) override
		{
			// What a layout is expected to answer when the index is not in the list.
			if (p_index < 0 || p_index >= m_items.size()) {
				return nullptr;
			}
			return m_items.takeAt(p_index);
		}
		Qt::Orientations expandingDirections() const override { return {}; }
		bool hasHeightForWidth() const override { return true; }
		int heightForWidth(int p_width) const override
		{
			return doLayout(QRect(0, 0, p_width, 0), true);
		}

		void setGeometry(const QRect& p_rect) override
		{
			QLayout::setGeometry(p_rect);
			doLayout(p_rect, false);
		}

		QSize sizeHint() const override { return minimumSize(); }

		QSize minimumSize() const override
		{
			QSize size;
			for (const QLayoutItem* item : m_items) {
				size = size.expandedTo(item->minimumSize());
			}
			return size;
		}

	private:
		/** @param p_testOnly only measures the height the items need for that width. */
		int doLayout(const QRect& p_rect, bool p_testOnly) const
		{
			int x = p_rect.x();
			int y = p_rect.y();
			int rowHeight = 0;
			for (QLayoutItem* item : m_items) {
				const QSize hint = item->sizeHint();
				int nextX = x + hint.width() + m_space;
				if (nextX - m_space > p_rect.right() + 1 && rowHeight > 0) {
					// No room left on this row: the next card starts a new one.
					x = p_rect.x();
					y += rowHeight + m_space;
					nextX = x + hint.width() + m_space;
					rowHeight = 0;
				}
				if (!p_testOnly) {
					item->setGeometry(QRect(QPoint(x, y), hint));
				}
				x = nextX;
				rowHeight = std::max(rowHeight, hint.height());
			}
			return y + rowHeight - p_rect.y();
		}

		QList<QLayoutItem*> m_items;
		int m_space = 12;
	};

	/** A card of the start page: the picture of a project, its name, and when it was
	 * opened last. Clicking the card asks for the project; the small button in its
	 * corner takes it off the list.
	 *
	 * It calls back rather than emitting: it is a private helper of the page, and a
	 * class with signals would have to live in a header of its own for moc to see it. */
	class ProjectCard : public QFrame
	{
	public:
		ProjectCard(const MOON::RecentProject& p_project, QWidget* p_parent)
			: QFrame(p_parent)
			, m_path(p_project.path)
			, m_name(p_project.name)
			, m_when(QObject::tr("Opened %1").arg(
				p_project.lastOpened.toString(QStringLiteral("yyyy-MM-dd HH:mm"))))
			, m_preview(p_project.preview)
		{
			setObjectName(QStringLiteral("ProjectCard"));
			setCursor(Qt::PointingHandCursor);
			setToolTip(p_project.path);
			// A card keeps its size: the page lays them out as a flow, and a card that
			// stretched to fill what is left of a row would make the rows uneven.
			setFixedSize(kCardWidth, kCardHeight);

			// The card is painted rather than laid out from child widgets: the whole
			// point of the animation is where the texts are at a given moment, and a
			// layout would put them back where it thinks they belong on every frame.
			auto* forget = new QToolButton(this);
			forget->setText(QStringLiteral("\u2715"));
			forget->setAutoRaise(true);
			forget->setToolTip(QObject::tr("Take this project off the list"));
			forget->setCursor(Qt::ArrowCursor);
			forget->setGeometry(kCardWidth - kForgetButtonSize - 4, 4,
				kForgetButtonSize, kForgetButtonSize);
			QObject::connect(forget, &QToolButton::clicked, this, [this]() {
				if (onForget) {
					onForget();
				}
				});

			// The animation the reference application runs on a card: 250 ms, eased
			// out, driving how far the texts have moved up over the picture.
			m_hover.setDuration(250);
			m_hover.setEasingCurve(QEasingCurve::OutQuad);
			QObject::connect(&m_hover, &QVariantAnimation::valueChanged, this,
				[this](const QVariant& p_value) {
					m_progress = p_value.toReal();
					update();
				});
		}

		/** What the card does when it is clicked, and when its corner button is. */
		std::function<void()> onClicked;
		std::function<void()> onForget;

	protected:
		void enterEvent(QEvent* p_event) override
		{
			animateTo(1.0);
			QFrame::enterEvent(p_event);
		}

		void leaveEvent(QEvent* p_event) override
		{
			animateTo(0.0);
			QFrame::leaveEvent(p_event);
		}

		void mousePressEvent(QMouseEvent* p_event) override
		{
			if (onClicked) {
				onClicked();
			}
			QFrame::mousePressEvent(p_event);
		}

		void paintEvent(QPaintEvent*) override
		{
			QPainter painter(this);
			painter.setRenderHint(QPainter::Antialiasing);
			const qreal progress = m_progress;

			// The frame: the accent colour of the application comes up as the pointer
			// arrives, and the body of the card lifts with it.
			const QColor idleBorder(58, 64, 72);
			const QColor accentBorder(90, 143, 216);
			const QColor idleBody(43, 47, 54);
			const QColor hoverBody(48, 54, 64);
			painter.setPen(QPen(mix(idleBorder, accentBorder, progress), 1.0));
			painter.setBrush(mix(idleBody, hoverBody, progress));
			painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);

			// The picture, zoomed a touch while the pointer is on the card, and shaded
			// from below with the same progress - that is what the texts move onto.
			const QRect pictureArea(kCardInset, kCardInset, kCardPictureWidth,
				kCardPictureHeight);
			painter.save();
			QPainterPath clip;
			clip.addRoundedRect(pictureArea, 3, 3);
			painter.setClipPath(clip);
			if (m_preview.isNull()) {
				// No picture of it yet: the icon of the file is what there is to show.
				const QPixmap icon = QIcon::fromTheme(
					QStringLiteral("document-open")).pixmap(64, 64);
				painter.drawPixmap(pictureArea.center() - QPoint(32, 32), icon);
			}
			else {
				const QRectF zoomed = QRectF(pictureArea).adjusted(
					-kCardHoverZoom * progress, -kCardHoverZoom * progress,
					kCardHoverZoom * progress, kCardHoverZoom * progress);
				painter.drawPixmap(zoomed, m_preview, QRectF(m_preview.rect()));
			}
			// The shade runs down the picture - clear at the top, dark at the bottom,
			// which is where the texts move onto.
			QLinearGradient shade(
				pictureArea.left(), pictureArea.top(),
				pictureArea.left(), pictureArea.bottom());
			shade.setColorAt(0.55, QColor(24, 27, 32, 0));
			shade.setColorAt(1.0, QColor(24, 27, 32, static_cast<int>(215 * progress)));
			painter.fillRect(pictureArea, shade);
			painter.restore();

			// The name and when it was opened last: they sit under the picture and move
			// up over it as the pointer arrives.
			const int lift = static_cast<int>(kCardHoverLift * progress);
			QFont nameFont = font();
			nameFont.setBold(true);
			nameFont.setPointSizeF(font().pointSizeF() + kCardNameFontDelta);
			painter.setFont(nameFont);
			painter.setPen(QColor(230, 232, 234));
			const QFontMetrics nameMetrics(nameFont);
			painter.drawText(
				QRect(kCardInset, pictureArea.bottom() + 8 - lift,
					kCardWidth - 2 * kCardInset, nameMetrics.height()),
				Qt::AlignLeft | Qt::AlignVCenter,
				nameMetrics.elidedText(m_name, Qt::ElideMiddle, kCardWidth - 2 * kCardInset));
			QFont whenFont = font();
			whenFont.setPointSizeF(font().pointSizeF() + kCardWhenFontDelta);
			const QFontMetrics whenMetrics(whenFont);
			painter.setFont(whenFont);
			painter.setPen(QColor(154, 160, 166));
			painter.drawText(
				QRect(kCardInset, pictureArea.bottom() + 10 + nameMetrics.height() - lift,
					kCardWidth - 2 * kCardInset, whenMetrics.height()),
				Qt::AlignLeft | Qt::AlignVCenter,
				whenMetrics.elidedText(
					m_when, Qt::ElideRight, kCardWidth - 2 * kCardInset));
		}

	private:
		static QColor mix(const QColor& p_from, const QColor& p_to, qreal p_progress)
		{
			return QColor(
				p_from.red() + (p_to.red() - p_from.red()) * p_progress,
				p_from.green() + (p_to.green() - p_from.green()) * p_progress,
				p_from.blue() + (p_to.blue() - p_from.blue()) * p_progress);
		}

		void animateTo(qreal p_target)
		{
			if (m_progress == p_target) {
				return;
			}
			m_hover.stop();
			m_hover.setStartValue(m_progress);
			m_hover.setEndValue(p_target);
			m_hover.start();
		}

		QString m_path;
		QString m_name;
		QString m_when;
		QPixmap m_preview;
		QVariantAnimation m_hover;
		qreal m_progress = 0.0;
	};

	QPushButton* makeTile(const QString& p_text, const QString& p_hint, QWidget* p_parent)
	{
		auto* button = new QPushButton(p_text, p_parent);
		button->setToolTip(p_hint);
		button->setMinimumSize(220, 56);
		button->setCursor(Qt::PointingHandCursor);
		button->setStyleSheet(QStringLiteral(
			"QPushButton { background: #33415c; border: 1px solid #46587c;"
			" border-radius: 4px; color: #e6e8ea; font-size: 14px; }"
			"QPushButton:hover { background: #3c4d6d; }"));
		return button;
	}
}

namespace MOON
{
	class HomePanel::Internal
	{
	public:
		QStackedWidget* stack = nullptr;
		int workspaceIndex = 0;
		FlowLayout* cards = nullptr;
		std::vector<QWidget*> cardWidgets;
	};

	HomePanel::HomePanel(QWidget* p_parent)
		: QWidget(p_parent)
		, mInternal(new Internal)
	{
		setObjectName(QStringLiteral("HomePanel"));
		// A plain QWidget paints a background from its stylesheet only when it is told
		// to: without this the page is see-through, and the workspace it is laid over
		// shows through everything that has no card on it.
		setAttribute(Qt::WA_StyledBackground, true);
		setStyleSheet(QStringLiteral("QWidget#HomePanel { background: #21242a; }"));

		auto* column = new QVBoxLayout(this);
		column->setContentsMargins(48, 40, 48, 40);
		column->setSpacing(18);

		auto* title = new QLabel(QStringLiteral("MOON"), this);
		title->setAlignment(Qt::AlignHCenter);
		title->setStyleSheet(
			QStringLiteral("color: #e6e8ea; font-size: 34px; font-weight: 700;"));
		column->addWidget(title);
		auto* subtitle = new QLabel(
			tr("Parametric modelling and CAE meshing - make a project, or open one"),
			this);
		subtitle->setAlignment(Qt::AlignHCenter);
		subtitle->setStyleSheet(QStringLiteral("color: #9aa0a6; font-size: 13px;"));
		column->addWidget(subtitle);

		auto* tiles = new QHBoxLayout();
		tiles->addStretch();
		auto* makeNew = makeTile(tr("New Project"), tr("Start an empty project"), this);
		auto* open = makeTile(tr("Open Project..."), tr("Open a .moon document"), this);
		tiles->addWidget(makeNew);
		tiles->addSpacing(16);
		tiles->addWidget(open);
		tiles->addStretch();
		column->addLayout(tiles);
		column->addSpacing(10);

		auto* recentLabel = new QLabel(tr("Recent projects"), this);
		recentLabel->setStyleSheet(QStringLiteral("color: #c8ccd0; font-size: 14px;"));
		column->addWidget(recentLabel);

		auto* scroll = new QScrollArea(this);
		scroll->setWidgetResizable(true);
		scroll->setFrameShape(QFrame::NoFrame);
		scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		auto* cardsHost = new QWidget(scroll);
		mInternal->cards = new FlowLayout(cardsHost);
		scroll->setWidget(cardsHost);
		column->addWidget(scroll, 1);

		connect(makeNew, &QPushButton::clicked, this, [this]() {
			emit newProjectRequested();
			});
		connect(open, &QPushButton::clicked, this, [this]() {
			emit openProjectRequested();
			});

		// The panel is a service so that anything that has just opened or closed a
		// document can put the right page in front of the user without having to know
		// who owns the stack.
		RegService(HomePanel, *this);
		RefreshRecentProjects();
	}

	HomePanel::~HomePanel()
	{
		delete mInternal;
	}

	void HomePanel::AttachWorkspace(QStackedWidget* p_stack, int p_workspaceIndex)
	{
		mInternal->stack = p_stack;
		mInternal->workspaceIndex = p_workspaceIndex;
	}

	void HomePanel::ShowStartPage()
	{
		RefreshRecentProjects();
		if (mInternal->stack != nullptr) {
			mInternal->stack->setCurrentWidget(this);
		}
		CORE_INFO("[Home] the start page is up");
	}

	void HomePanel::ShowWorkspace()
	{
		if (mInternal->stack != nullptr) {
			mInternal->stack->setCurrentIndex(mInternal->workspaceIndex);
		}
		CORE_INFO("[Home] the workspace is up");
	}

	void HomePanel::RefreshRecentProjects()
	{
		for (QWidget* card : mInternal->cardWidgets) {
			mInternal->cards->removeWidget(card);
			card->deleteLater();
		}
		mInternal->cardWidgets.clear();

		const std::vector<RecentProject> projects = RecentProjects::Load();
		for (int i = 0; i < static_cast<int>(projects.size()); ++i) {
			auto* card = new ProjectCard(projects[i], this);
			const QString path = projects[i].path;
			card->onClicked = [this, path]() { emit projectRequested(path); };
			card->onForget = [this, path]() {
				RecentProjects::Forget(path);
				RefreshRecentProjects();
				emit projectForgotten(path);
				};
			mInternal->cards->addWidget(card);
			mInternal->cardWidgets.push_back(card);
		}
		if (projects.empty()) {
			auto* none = new QLabel(
				tr("No project has been opened yet - make one, or open one."), this);
			none->setStyleSheet(QStringLiteral("color: #8a9096;"));
			mInternal->cards->addWidget(none);
			mInternal->cardWidgets.push_back(none);
		}
		CORE_INFO("[Home] {0} recent project(s)", projects.size());
	}
}
