#include "LogPanel.h"
#include "editor/Language.h"
#include "core/log.h"
#include "Core/Global/ServiceLocator.h"
#include "editor/UI/DockWidgetTitleBar.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QColor>
#include <QDateTime>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QVBoxLayout>

namespace MOON {

	namespace {
		// Field widths in monospace characters; they keep the header and the
		// log lines aligned column by column.
		constexpr int LevelFieldWidth = 8;   // "[info]:" + one space
		constexpr int TimeFieldWidth = 8;    // "21:10:50" + padding
		constexpr int TimeSepWidth = 3;      // " > " after the timestamp

		QString levelName(int level) {
			switch (level) {
				case LogOutput::LL_DEBUG:
					return "debug";
				case LogOutput::LL_WARNING:
					return "warning";
				case LogOutput::LL_ERROR:
					return "error";
				case LogOutput::LL_FATAL:
					return "fatal";
				default:
					return "info";
			}
		}
		QColor levelColor(int level) {
			switch (level) {
				case LogOutput::LL_WARNING:
					return QColor("#e5c07b");
				case LogOutput::LL_ERROR:
				case LogOutput::LL_FATAL:
					return QColor("#e06c75");
				case LogOutput::LL_DEBUG:
					return QColor("#7f8c98");
				default:
					return QColor("#98c379");
			}
		}
	}

	LogPanel::LogPanel(QWidget* parent)
		: QDockWidget(parent), LogOutput("LogPanel") {
		RegService(LogPanel, *this);
		MOON::Language::SetWindowTitle(this, QStringLiteral("日志"), QStringLiteral("Log"));
		auto* titleBar = new DockWidgetTitleBar(this);
		setTitleBarWidget(titleBar);

		// Level filters live in the title bar, keeping the content area for
		// the log text itself.
		m_debugCheck = new QCheckBox(titleBar);
		MOON::Language::SetText(m_debugCheck, QStringLiteral("调试"), QStringLiteral("Debug"));
		m_infoCheck = new QCheckBox(titleBar);
		MOON::Language::SetText(m_infoCheck, QStringLiteral("信息"), QStringLiteral("Info"));
		m_warnCheck = new QCheckBox(titleBar);
		MOON::Language::SetText(m_warnCheck, QStringLiteral("警告"), QStringLiteral("Warning"));
		m_errorCheck = new QCheckBox(titleBar);
		MOON::Language::SetText(m_errorCheck, QStringLiteral("错误"), QStringLiteral("Error"));
		m_debugCheck->setChecked(true);
		m_infoCheck->setChecked(true);
		m_warnCheck->setChecked(true);
		m_errorCheck->setChecked(true);
		m_clear = new QPushButton(titleBar);
		MOON::Language::SetText(m_clear, QStringLiteral("清空"), QStringLiteral("Clean"));
		for (QWidget* w : {static_cast<QWidget*>(m_debugCheck), static_cast<QWidget*>(m_infoCheck),
		                   static_cast<QWidget*>(m_warnCheck), static_cast<QWidget*>(m_errorCheck),
		                   static_cast<QWidget*>(m_clear)}) {
			w->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
			titleBar->addTitleWidget(w);
		}

		QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
		mono.setPointSize(mono.pointSize() + 2);
		const QFontMetrics fm(mono);
		const int charWidth = fm.horizontalAdvance(QStringLiteral("M"));

		auto* content = new QWidget(this);
		auto* mainLayout = new QVBoxLayout(content);
		mainLayout->setContentsMargins(3, 3, 3, 3);
		mainLayout->setSpacing(2);

		// Header row: widths match the padded monospace content columns.
		//auto* headerRow = new QHBoxLayout();
		//headerRow->setSpacing(0);
		//auto* timeHeader = new QLabel(tr("Time"), content);
		//auto* levelHeader = new QLabel(tr("Level"), content);
		//auto* msgHeader = new QLabel(tr("Message"), content);
		//timeHeader->setFixedWidth((TimeFieldWidth + TimeSepWidth) * charWidth);
		//levelHeader->setFixedWidth(LevelFieldWidth * charWidth);
		//headerRow->addWidget(timeHeader);
		//headerRow->addWidget(levelHeader);
		//headerRow->addWidget(msgHeader, 1);
		//mainLayout->addLayout(headerRow);

		m_logText = new QPlainTextEdit(content);
		m_logText->setFont(mono);
		m_logText->setReadOnly(true);
		m_logText->setMaximumBlockCount(10000);
		m_logText->document()->setDocumentMargin(0);
		m_logText->setFrameShape(QFrame::NoFrame);
		m_logText->setContextMenuPolicy(Qt::CustomContextMenu);
		mainLayout->addWidget(m_logText, 1);

		setWidget(content);

		connect(this, &LogPanel::postMessage, this, &LogPanel::onLogMessage, Qt::QueuedConnection);
		connect(m_clear, &QPushButton::clicked, this, &LogPanel::onClearMessage);
		connect(m_debugCheck, &QCheckBox::toggled, this, &LogPanel::rebuildView);
		connect(m_infoCheck, &QCheckBox::toggled, this, &LogPanel::rebuildView);
		connect(m_warnCheck, &QCheckBox::toggled, this, &LogPanel::rebuildView);
		connect(m_errorCheck, &QCheckBox::toggled, this, &LogPanel::rebuildView);
		connect(m_logText, &QPlainTextEdit::customContextMenuRequested,
		        this, &LogPanel::showMenu);

		Log::intance().addOutput(this);
	}

	LogPanel::~LogPanel() {
		Log::intance().removeOutput(this);
	}

	void LogPanel::logMessage(Level level, const std::string& msg) {
		emit postMessage(level, QString::fromStdString(msg));
	}

	void LogPanel::onLogMessage(int level, QString msg) {
		const QString time = QDateTime::currentDateTime().toString("H:mm:ss");
		// Left-pad level and time to fixed monospace widths so every line lines
		// up under the corresponding header.
		QString levelField = QStringLiteral("[%1]:").arg(levelName(level));
		if (levelField.size() < LevelFieldWidth) {
			levelField = levelField.leftJustified(LevelFieldWidth, QLatin1Char(' '));
		}
		else {
			levelField += QLatin1Char(' ');
		}
		const QString line = QString("%1%2%3%4")
		                         .arg(time, -TimeFieldWidth)
		                         .arg(QStringLiteral(" > "))
		                         .arg(levelField)
		                         .arg(msg.trimmed());
		m_messages.push_back({ level, line });
		if (static_cast<int>(m_messages.size()) > 10000) {
			m_messages.pop_front();
		}
		if (levelVisible(level)) {
			appendMessage(level, line);
		}
	}

	void LogPanel::appendMessage(int level, const QString& line) {
		QTextCursor cursor(m_logText->document());
		cursor.movePosition(QTextCursor::End);
		QTextCharFormat fmt;
		fmt.setForeground(levelColor(level));
		cursor.setCharFormat(fmt);
		cursor.insertText(line + "\n");
		m_logText->verticalScrollBar()->setValue(m_logText->verticalScrollBar()->maximum());
	}

	bool LogPanel::levelVisible(int level) const {
		switch (level) {
			case LogOutput::LL_DEBUG:
				return m_debugCheck->isChecked();
			case LogOutput::LL_INFO:
				return m_infoCheck->isChecked();
			case LogOutput::LL_WARNING:
				return m_warnCheck->isChecked();
			case LogOutput::LL_ERROR:
			case LogOutput::LL_FATAL:
				return m_errorCheck->isChecked();
			default:
				return true;
		}
	}

	void LogPanel::rebuildView() {
		m_logText->setUpdatesEnabled(false);
		m_logText->clear();
		for (const auto& [level, line] : m_messages) {
			if (levelVisible(level)) {
				appendMessage(level, line);
			}
		}
		m_logText->setUpdatesEnabled(true);
	}

	void LogPanel::onClearMessage() {
		m_messages.clear();
		m_logText->clear();
	}

	void LogPanel::showMenu(const QPoint&) {
		QMenu menu(this);
		QAction* copy = menu.addAction(tr("Copy"));
		connect(copy, &QAction::triggered, this, &LogPanel::copyLogContent);
		menu.exec(QCursor::pos());
	}

	void LogPanel::copyLogContent() {
		QApplication::clipboard()->setText(m_logText->textCursor().selectedText());
	}

}
