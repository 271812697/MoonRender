#include "editor/Theme.h"

#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QFile>
#include <QFont>
#include <QFontMetrics>
#include <QLabel>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QSettings>
#include <QToolButton>
#include <QSvgRenderer>
#include <QVector>
#include <QWidget>
#include <QtGlobal>

#include <algorithm>
#include <cstdlib>

namespace
{
	const char* const kSettingKey = "ui/theme";

	/** The dark scheme: a cool dark grey, the accent blue the icons use, and white structure
	 * strokes. */
	const MOON::ThemeTokens kDark = []
	{
		MOON::ThemeTokens t;
		t.window = QColor(30, 35, 41);
		t.surface = QColor(38, 44, 51);
		t.well = QColor(25, 30, 36);
		t.border = QColor(47, 53, 61);
		t.text = QColor(207, 214, 223);
		t.textDim = QColor(139, 149, 163);
		t.textDisabled = QColor(102, 112, 124);
		t.accent = QColor(79, 163, 255);
		t.accentText = QColor(11, 24, 34);
		t.separator = QColor(42, 48, 56);
		t.barTop = "rgba(25, 25, 25, 127)";
		t.barBottom = "rgba(53, 53, 53, 75)";
		t.barBorder = "rgba(25, 25, 25, 75)";
		t.hoverWash = "rgba(255, 255, 255, 0.06)";
		t.pressedWash = "rgba(255, 255, 255, 0.11)";
		t.checkedWash = "rgba(79, 156, 240, 0.22)";
		t.menuSelection = "rgba(106, 106, 106, 255)";
		t.iconLine = QColor(230, 237, 246);
		t.iconEdge = QColor(255, 255, 255);
		return t;
	}();

	/** The light scheme: paper-white surfaces, a deeper blue for contrast on white (the dark
	 * scheme's #4fa3ff only reaches 2.4:1 there), and dark structure strokes for the icons. */
	const MOON::ThemeTokens kLight = []
	{
		MOON::ThemeTokens t;
		t.window = QColor(242, 244, 247);
		t.surface = QColor(255, 255, 255);
		t.well = QColor(233, 236, 241);
		t.border = QColor(211, 217, 224);
		t.text = QColor(31, 41, 51);
		t.textDim = QColor(91, 103, 115);
		t.textDisabled = QColor(154, 165, 177);
		t.accent = QColor(43, 124, 211);
		t.accentText = QColor(255, 255, 255);
		t.separator = QColor(214, 220, 227);
		t.barTop = "rgba(255, 255, 255, 235)";
		t.barBottom = "rgba(226, 231, 237, 255)";
		t.barBorder = "rgba(199, 206, 214, 255)";
		t.hoverWash = "rgba(15, 30, 50, 0.06)";
		t.pressedWash = "rgba(15, 30, 50, 0.12)";
		t.checkedWash = "rgba(43, 124, 211, 0.16)";
		t.menuSelection = "rgba(43, 124, 211, 0.18)";
		t.iconLine = QColor(70, 84, 98);
		t.iconEdge = QColor(43, 54, 66);
		return t;
	}();

	MOON::Theme::ETheme gCurrent = MOON::Theme::ETheme::Dark;
}

MOON::Theme::ETheme MOON::Theme::Current()
{
	return gCurrent;
}

void MOON::Theme::Set(Theme::ETheme p_theme, bool p_persist)
{
	gCurrent = p_theme;
	if (p_persist)
	{
		QSettings settings;
		settings.setValue(kSettingKey, p_theme == ETheme::Light ? "light" : "dark");
	}
}

void MOON::Theme::Initialize()
{
	// The environment wins, so a screenshot can be taken in the other scheme without touching
	// what the user picked last time.
	if (const char* p_env = std::getenv("MOON_THEME"); p_env != nullptr && *p_env != '\0')
	{
		gCurrent = QByteArray(p_env).toLower() == "light" ? ETheme::Light : ETheme::Dark;
		return;
	}
	QSettings settings;
	const QString stored = settings.value(kSettingKey, "dark").toString();
	gCurrent = stored == "light" ? ETheme::Light : ETheme::Dark;
}

const MOON::ThemeTokens& MOON::Theme::Tokens()
{
	return gCurrent == ETheme::Light ? kLight : kDark;
}

QString MOON::Theme::Style(QString p_css)
{
	const ThemeTokens& t = Tokens();
	// Longest token first: "@textDim" and "@textDisabled" both start with "@text", and a naive
	// in-order replace turns them into "#1f2933Dim" (which the Qt style parser then rejects).
	QVector<QPair<QString, QString>> tokens = {
		{ "@textDisabled", t.textDisabled.name(QColor::HexRgb) },
		{ "@textDim", t.textDim.name(QColor::HexRgb) },
		{ "@text", t.text.name(QColor::HexRgb) },
		{ "@accentText", t.accentText.name(QColor::HexRgb) },
		{ "@accent", t.accent.name(QColor::HexRgb) },
		{ "@separator", t.separator.name(QColor::HexRgb) },
		{ "@window", t.window.name(QColor::HexRgb) },
		{ "@surface", t.surface.name(QColor::HexRgb) },
		{ "@well", t.well.name(QColor::HexRgb) },
		{ "@border", t.border.name(QColor::HexRgb) },
		{ "@barTop", t.barTop },
		{ "@barBottom", t.barBottom },
		{ "@barBorder", t.barBorder },
		{ "@hoverWash", t.hoverWash },
		{ "@pressedWash", t.pressedWash },
		{ "@checkedWash", t.checkedWash },
		{ "@menuSelection", t.menuSelection },
	};
	std::sort(tokens.begin(), tokens.end(),
		[](const QPair<QString, QString>& p_left, const QPair<QString, QString>& p_right)
		{
			return p_left.first.size() > p_right.first.size();
		});
	for (const auto& token : tokens)
	{
		p_css.replace(token.first, token.second);
	}
	return p_css;
}
QIcon MOON::Theme::Icon(const QString& p_path)
{
	QFile file(p_path);
	if (!file.open(QIODevice::ReadOnly))
	{
		return QIcon(p_path);
	}
	QByteArray svg = file.readAll();
	const ThemeTokens& t = Tokens();
	if (svg.contains("#LINE#"))
	{
		svg.replace("#LINE#", t.iconLine.name(QColor::HexRgb).toUtf8());
	}
	if (svg.contains("#EDGE#"))
	{
		svg.replace("#EDGE#", t.iconEdge.name(QColor::HexRgb).toUtf8());
	}

	QSvgRenderer renderer(svg);
	if (!renderer.isValid())
	{
		return QIcon(p_path);
	}

	// One pixmap per size the ribbon asks for; Qt picks the closest and does not have to scale
	// a single large image down.
	QIcon icon;
	for (int size : { 16, 24, 32, 48, 64 })
	{
		QPixmap pixmap(size, size);
		pixmap.fill(Qt::transparent);
		QPainter painter(&pixmap);
		painter.setRenderHint(QPainter::Antialiasing);
		painter.setRenderHint(QPainter::SmoothPixmapTransform);
		renderer.render(&painter, QRectF(0, 0, size, size));
		painter.end();
		icon.addPixmap(pixmap);
	}
	return icon;
}

QString MOON::Theme::RibbonButtonStyle(int p_labelPx)
{
	const ThemeTokens& t = Tokens();
	QString css = R"(
		QToolButton {
			color: @text;
			font-size: %FONTPX%px;
			background: transparent;
			border: 1px solid transparent;
			border-radius: 5px;
			padding: 2px;
		}
		QToolButton:hover {
			background: @hoverWash;
			border-color: @border;
		}
		QToolButton:pressed {
			background: @pressedWash;
		}
		QToolButton:checked {
			background: @checkedWash;
			border-color: @accent;
			color: @text;
		}
		QToolButton:disabled {
			color: @textDisabled;
		}
	)";
	css.replace("%FONTPX%", QString::number(p_labelPx));
	// Through Style() so the "@text" / "@textDisabled" prefix order is handled in one place.
	return Theme::Style(css);
}

void MOON::Theme::Apply(QApplication* p_app)
{
	if (p_app == nullptr)
	{
		return;
	}
	const ThemeTokens& t = Tokens();

	QPalette palette;
	palette.setColor(QPalette::Window, t.window);
	palette.setColor(QPalette::WindowText, t.text);
	palette.setColor(QPalette::Disabled, QPalette::WindowText, t.textDisabled);
	palette.setColor(QPalette::Base, t.well);
	palette.setColor(QPalette::AlternateBase, t.surface);
	palette.setColor(QPalette::ToolTipBase, t.surface);
	palette.setColor(QPalette::ToolTipText, t.text);
	palette.setColor(QPalette::Text, t.text);
	palette.setColor(QPalette::Disabled, QPalette::Text, t.textDisabled);
	palette.setColor(QPalette::Dark, t.window.darker(115));
	palette.setColor(QPalette::Shadow, t.window.darker(135));
	palette.setColor(QPalette::Mid, t.border);
	palette.setColor(QPalette::Light, t.surface.lighter(115));
	palette.setColor(QPalette::Button, t.surface);
	palette.setColor(QPalette::ButtonText, t.text);
	palette.setColor(QPalette::Disabled, QPalette::ButtonText, t.textDisabled);
	palette.setColor(QPalette::BrightText, Qt::white);
	palette.setColor(QPalette::Link, t.accent);
	palette.setColor(QPalette::Highlight, t.accent);
	palette.setColor(QPalette::HighlightedText, t.accentText);
	palette.setColor(QPalette::Disabled, QPalette::Highlight, t.border);
	palette.setColor(QPalette::Disabled, QPalette::HighlightedText, t.textDisabled);
	p_app->setPalette(palette);

	QFile qssFile(QStringLiteral(":/darkstyle/darkstyle.qss"));
	if (qssFile.open(QIODevice::ReadOnly | QIODevice::Text))
	{
		QString css = QString::fromUtf8(qssFile.readAll());
		const ThemeTokens& tokens = Tokens();
		css.replace("rgba(25,25,25,127)", tokens.barTop);
		css.replace("rgba(53,53,53,75)", tokens.barBottom);
		css.replace("rgba(25,25,25,75)", tokens.barBorder);
		css.replace("rgba(25,25,25,255)", tokens.window.name(QColor::HexRgb));
		css.replace("rgba(53,53,53,127)", tokens.surface.name(QColor::HexRgb));
		css.replace("rgba(106,106,106,255)", tokens.menuSelection);
		css.replace("rgba(106,106,106,75)", tokens.hoverWash);
		css.replace("rgba(35,35,35,127)", tokens.well.name(QColor::HexRgb));
		css.replace("rgba(255,106,106,127)", tokens.accent.name(QColor::HexRgb));
		css.replace("rgba(147,191,236,127)", tokens.accent.name(QColor::HexRgb));
		// Contrast overrides for the places where the dark stylesheet leaned on light text on a
		// dark plate: those labels and titles have to follow the scheme's text colour.
		css += Style(QStringLiteral(R"(
			QDockWidget { color: @text; }
			QDockWidget::title { color: @text; }
			QWidget#GroupTitleBar, QWidget#GroupTitleBar QLabel, QWidget#GroupTitleBar QToolButton { color: @text; }
			QToolTip { color: @text; }
			QStatusBar { color: @textDim; }
			QToolBar { color: @text; }
			QTabWidget::pane { border-color: @border; }
			QLabel, QCheckBox, QRadioButton, QGroupBox { color: @text; }
			QPushButton { color: @text; }
			QPushButton:disabled, QCheckBox:disabled, QRadioButton:disabled { color: @textDisabled; }
		)"));
		p_app->setStyleSheet(css);
		if (const char* p_dump = std::getenv("MOON_DUMP_QSS"); p_dump != nullptr && *p_dump != '\0')
		{
			QFile out(QString::fromLocal8Bit(p_dump));
			if (out.open(QIODevice::WriteOnly | QIODevice::Text)) { out.write(css.toUtf8()); }
		}
	}

	// Every icon that went through Theme::Icon remembers where it came from, and every ribbon
	// button remembers its label size, so the whole UI can change scheme without being rebuilt.
	for (QWidget* widget : QApplication::allWidgets())
	{
		for (QAction* action : widget->findChildren<QAction*>())
		{
			const QVariant path = action->property("moonIcon");
			if (path.isValid())
			{
				action->setIcon(Icon(path.toString()));
			}
		}
		if (auto* button = qobject_cast<QToolButton*>(widget))
		{
			const QVariant labelPx = button->property("moonLabelPx");
			if (labelPx.isValid())
			{
				button->setStyleSheet(RibbonButtonStyle(labelPx.toInt()));
			}
		}
		if (widget->property("moonStyle").isValid())
		{
			widget->setStyleSheet(Style(widget->property("moonStyle").toString()));
		}
		if (auto* label = qobject_cast<QLabel*>(widget))
		{
			if (label->property("moonCaption").toBool())
			{
				label->setStyleSheet(QString("color: %1;").arg(t.textDim.name(QColor::HexRgb)));
			}
			if (label->property("moonTitle").toBool())
			{
				label->setStyleSheet(QString("color: %1;").arg(t.text.name(QColor::HexRgb)));
			}
		}
	}
}
