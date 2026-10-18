#include "MaterialEditorPanel.h"
#include "MaterialCodeEditor.h"
#include "MaterialPreviewWidget.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QShortcut>
#include <QSplitter>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QToolButton>
#include <QVBoxLayout>

#include "Core/Global/ServiceLocator.h"
#include "editor/UI/DockWidgetTitleBar.h"
#include "renderer/Context.h"
#include "Rendering/Context/Driver.h"
#include "Rendering/Resources/Loaders/ShaderLoader.h"
#include "Tools/Utils/PathParser.h"

#include "Matc.h"

namespace
{
	// The output colours and the status dot: the same family as the editor's palette, so a
	// failure reads the same colour wherever it is reported.
	const QColor kLogInfo(0x9d, 0x99, 0x95);
	const QColor kLogSuccess(0x8f, 0xc3, 0x65);
	const QColor kLogWarning(0xe5, 0xc0, 0x7b);
	const QColor kLogError(0xe0, 0x6c, 0x75);
	const QColor kStatusIdle(0x6f, 0x75, 0x7e);

	/** ":Shaders\\PostProcess\\Blur.ovfx" for "PostProcess/Blur.ovfx", which is what the archive
	 * and the shader manager are keyed by. */
	std::string ToShaderKey(const std::string& p_relative)
	{
		std::string key = ":Shaders\\";
		for (const char c : p_relative)
		{
			key += (c == '/' ? '\\' : c);
		}
		return key;
	}

	/** A section strip in the flat style of Inviwo's processor headers: a shade darker than the
	 * content, one hairline under it, an upper-case label, and an optional trailing widget (the
	 * cursor readout, a Clear button). */
	QWidget* MakeSectionHeader(const QString& p_title, QWidget* p_parent, QWidget* p_trailing = nullptr)
	{
		auto* header = new QFrame(p_parent);
		header->setObjectName("MaterialSectionHeader");
		header->setFixedHeight(22);

		auto* layout = new QHBoxLayout(header);
		layout->setContentsMargins(8, 0, 4, 0);
		layout->setSpacing(8);

		auto* title = new QLabel(p_title, header);
		title->setObjectName("MaterialSectionTitle");
		layout->addWidget(title);
		layout->addStretch(1);
		if (p_trailing != nullptr)
		{
			layout->addWidget(p_trailing);
		}
		return header;
	}
}

class MOON::MaterialEditorPanel::MaterialEditorPanelInternal
{
public:
	MaterialEditorPanelInternal(MaterialEditorPanel* p_owner) : m_owner(p_owner)
	{
		m_shaderRoot = std::filesystem::path(Tools::Utils::PathParser::GetExeDirectory())
			/ "Moon" / "Data" / "Engine" / "Shaders";

		BuildUi();
		RefreshFileList();

		if (m_filePicker->count() > 0)
		{
			m_filePicker->setCurrentIndex(0);
		}

		// Nothing GL happens here: the first preview is compiled when the panel is shown (see
		// OnFirstShow), and the widget it is handed to builds the shader on its own paint, once
		// the render module's context exists.
	}

	void BuildUi()
	{
		// Side by side needs the width: below this the two halves stop being useful, so the
		// dock asks for it instead of squeezing and eliding everything.
		m_owner->setMinimumWidth(480);

		// The dock's own title bar, like the other panels of the workspace.
		auto* titleBar = new DockWidgetTitleBar(m_owner);
		m_owner->setTitleBarWidget(titleBar);

		// A dot next to the title that says, at a glance, what the last compile did.
		m_status = new QFrame(titleBar);
		m_status->setFixedSize(8, 8);
		m_status->setStyleSheet(QString::fromLatin1("background: #6f757e; border-radius: 4px;"));
		m_status->setToolTip(QStringLiteral("Nothing compiled yet"));
		titleBar->addTitleWidget(m_status);

		auto* content = new QWidget(m_owner);
		content->setObjectName("MaterialEditorContent");
		content->setAttribute(Qt::WA_StyledBackground, true);
		auto* layout = new QVBoxLayout(content);
		layout->setContentsMargins(0, 0, 0, 0);
		layout->setSpacing(0);

		// --- the file and the two actions ------------------------------------------------
		auto* toolbar = new QWidget(content);
		toolbar->setObjectName("MaterialToolbar");
		toolbar->setAttribute(Qt::WA_StyledBackground, true);
		auto* toolbarLayout = new QHBoxLayout(toolbar);
		toolbarLayout->setContentsMargins(6, 5, 6, 5);
		toolbarLayout->setSpacing(4);

		m_filePicker = new QComboBox(toolbar);
		m_filePicker->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		m_filePicker->setMinimumContentsLength(14);
		m_filePicker->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLength);
		m_filePicker->setMaxVisibleItems(18);
		m_filePicker->setToolTip(QStringLiteral("The material under Engine/Shaders being edited"));

		m_apply = new QToolButton(toolbar);
		m_apply->setObjectName("MaterialPrimaryButton");
		m_apply->setText(QStringLiteral("Apply"));
		m_apply->setToolTip(QStringLiteral("Compile the source and reload the shader wherever it is used (Ctrl+Enter)"));
		m_apply->setCursor(Qt::PointingHandCursor);

		m_reload = new QToolButton(toolbar);
		m_reload->setObjectName("MaterialButton");
		m_reload->setText(QStringLiteral("Revert"));
		m_reload->setToolTip(QStringLiteral("Discard the edits and read the file from disk again"));
		m_reload->setCursor(Qt::PointingHandCursor);

		toolbarLayout->addWidget(m_filePicker, 1);
		toolbarLayout->addWidget(m_apply);
		toolbarLayout->addWidget(m_reload);
		layout->addWidget(toolbar);

		// --- preview on the left, source and output on the right --------------------------
		// Side by side on purpose: the panel exists to compare what the material looks like
		// with the source that produces it, so both have to be visible at once.
		auto* columns = new QSplitter(Qt::Horizontal, content);
		columns->setHandleWidth(4);
		columns->setChildrenCollapsible(false);

		// The preview is a GL widget with a render module view behind it (see
		// MaterialPreviewWidget); the frame around it separates the rendering from the widgets.
		auto* preview = new QWidget(columns);
		auto* previewLayout = new QVBoxLayout(preview);
		previewLayout->setContentsMargins(0, 0, 0, 0);
		previewLayout->setSpacing(0);
		auto* previewHint = new QLabel(QStringLiteral("Drag to orbit, wheel to zoom"), preview);
		previewHint->setObjectName("MaterialSectionHint");
		previewLayout->addWidget(MakeSectionHeader(QStringLiteral("PREVIEW"), preview, previewHint));

		auto* previewFrame = new QFrame(preview);
		previewFrame->setObjectName("MaterialPreviewFrame");
		auto* previewFrameLayout = new QVBoxLayout(previewFrame);
		previewFrameLayout->setContentsMargins(1, 1, 1, 1);
		previewFrameLayout->setSpacing(0);
		m_preview = new ::MOON::MaterialPreviewWidget(previewFrame);
		previewFrameLayout->addWidget(m_preview);
		previewLayout->addWidget(previewFrame, 1);

		// The right hand side: the source with the output under it, so the log stays next to
		// the code that produced it.
		auto* rightColumn = new QSplitter(Qt::Vertical, columns);
		rightColumn->setHandleWidth(4);
		rightColumn->setChildrenCollapsible(false);

		auto* source = new QWidget(rightColumn);
		auto* sourceLayout = new QVBoxLayout(source);
		sourceLayout->setContentsMargins(0, 0, 0, 0);
		sourceLayout->setSpacing(0);
		m_position = new QLabel(QStringLiteral("Ln 1, Col 1"), source);
		m_position->setObjectName("MaterialSectionHint");
		sourceLayout->addWidget(MakeSectionHeader(QStringLiteral("SOURCE"), source, m_position));
		m_source = new ::MOON::MaterialCodeEditor(source);
		sourceLayout->addWidget(m_source, 1);

		auto* output = new QWidget(rightColumn);
		auto* outputLayout = new QVBoxLayout(output);
		outputLayout->setContentsMargins(0, 0, 0, 0);
		outputLayout->setSpacing(0);
		m_clear = new QToolButton(output);
		m_clear->setObjectName("MaterialLinkButton");
		m_clear->setText(QStringLiteral("Clear"));
		m_clear->setCursor(Qt::PointingHandCursor);
		m_clear->setToolTip(QStringLiteral("Clear the output"));
		outputLayout->addWidget(MakeSectionHeader(QStringLiteral("OUTPUT"), output, m_clear));

		m_log = new QPlainTextEdit(output);
		m_log->setObjectName("MaterialLogView");
		m_log->setReadOnly(true);
		m_log->setFrameShape(QFrame::NoFrame);
		m_log->setMaximumBlockCount(1000);
		m_log->setFont(::MOON::MaterialCodeEditor::CodeFont());
		m_log->document()->setDocumentMargin(6.0);
		// The stylesheet paints the widget; the palette is what Qt's text machinery paints the
		// viewport and the selection with, so both are set to the same colour.
		QPalette logPalette = m_log->palette();
		logPalette.setColor(QPalette::Base, QColor(0x2b, 0x2c, 0x27));
		logPalette.setColor(QPalette::Text, QColor(0x9d, 0x99, 0x95));
		logPalette.setColor(QPalette::Highlight, QColor(0x49, 0x42, 0x57));
		m_log->setPalette(logPalette);
		outputLayout->addWidget(m_log, 1);

		rightColumn->addWidget(source);
		rightColumn->addWidget(output);
		rightColumn->setStretchFactor(0, 1);
		rightColumn->setStretchFactor(1, 0);
		rightColumn->setSizes({ 420, 130 });

		columns->addWidget(preview);
		columns->addWidget(rightColumn);
		columns->setStretchFactor(0, 1);
		columns->setStretchFactor(1, 1);
		columns->setSizes({ 260, 360 });

		layout->addWidget(columns, 1);
		content->setLayout(layout);
		m_owner->setWidget(content);

		ApplyStyle(content);

		QObject::connect(m_filePicker, QOverload<int>::of(&QComboBox::currentIndexChanged), m_owner,
			[this](int p_index) { OnFileSelected(p_index); });
		QObject::connect(m_apply, &QToolButton::clicked, m_owner, [this] { Apply(); });
		QObject::connect(m_reload, &QToolButton::clicked, m_owner,
			[this] { LoadSelectedSource(); });

		// The usual "run" of an editor: keep the hands on the keyboard while iterating.
		auto* applyShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), m_owner);
		applyShortcut->setContext(Qt::WidgetWithChildrenShortcut);
		QObject::connect(applyShortcut, &QShortcut::activated, m_owner, [this] { Apply(); });
		QObject::connect(m_clear, &QToolButton::clicked, m_owner, [this] { m_log->clear(); });
		QObject::connect(m_source, &QPlainTextEdit::cursorPositionChanged, m_owner, [this] {
			const QTextCursor cursor = m_source->textCursor();
			m_position->setText(QStringLiteral("Ln %1, Col %2")
				.arg(cursor.blockNumber() + 1)
				.arg(cursor.positionInBlock() + 1));
		});
		// The preview widget compiles the GLSL itself (it has the module's context current when
		// it paints); a rejection comes back here with the driver's message.
		QObject::connect(m_preview, &::MOON::MaterialPreviewWidget::shaderFailed, m_owner,
			[this](const QString& p_error) {
				Log("[Shader] " + p_error.toStdString(), kLogError);
				SetStatus(kLogError, QStringLiteral("Shader rejected"));
			});
	}

	/** The panel's own stylesheet: flat strips, a hairline between the areas, the gradient
	 * buttons and the flat combo Inviwo's theme uses. Scoped to this panel's content so it does
	 * not leak into the rest of the editor. */
	void ApplyStyle(QWidget* p_content)
	{
		p_content->setStyleSheet(QString::fromLatin1(R"(
			QWidget#MaterialEditorContent { background: #323234; }
			QWidget#MaterialToolbar { background: #323234; border-bottom: 1px solid #1b1b1d; }
			QFrame#MaterialSectionHeader {
				background: #3a3a3d;
				border-top: 1px solid #1b1b1d;
				border-bottom: 1px solid #262629;
			}
			QLabel#MaterialSectionTitle { color: #c8ccd0; font-size: 11px; font-weight: 600; }
			QLabel#MaterialSectionHint { color: #7d8288; font-size: 11px; padding-right: 6px; }
			QComboBox {
				color: #c8ccd0;
				background: transparent;
				border: 1px solid transparent;
				border-radius: 2px;
				padding: 3px 6px;
				min-height: 18px;
			}
			QComboBox:hover, QComboBox:focus, QComboBox:on {
				background: #47474b;
				border: 1px solid #268bd2;
			}
			QComboBox::drop-down { border: none; width: 18px; }
			QComboBox::down-arrow { image: url(:/icons/arrow_down.svg); width: 10px; height: 10px; }
			QComboBox QAbstractItemView {
				background: #3a3a3d;
				color: #c8ccd0;
				border: 1px solid #1b1b1d;
				selection-background-color: #47474b;
			}
			QToolButton#MaterialPrimaryButton {
				color: #e8e8e8;
				padding: 4px 14px;
				border: 1px solid #1b1b1d;
				border-radius: 3px;
				background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
					stop:0 #6f6f75, stop:0.12 #144c72, stop:0.9 #144c72, stop:1 #222224);
			}
			QToolButton#MaterialPrimaryButton:hover { border-color: #268bd2; }
			QToolButton#MaterialPrimaryButton:pressed { padding-top: 5px; padding-left: 15px; }
			QToolButton#MaterialButton {
				color: #d7dae0;
				padding: 4px 12px;
				border: 1px solid #1b1b1d;
				border-radius: 3px;
				background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
					stop:0 #6f6f75, stop:0.12 #323235, stop:0.9 #323235, stop:1 #222224);
			}
			QToolButton#MaterialButton:hover { border-color: #268bd2; }
			QToolButton#MaterialButton:pressed { padding-top: 5px; padding-left: 13px; }
			QToolButton#MaterialLinkButton {
				color: #8f9398;
				background: transparent;
				border: none;
				padding: 0 4px;
			}
			QToolButton#MaterialLinkButton:hover { color: #e9e4de; }
			QFrame#MaterialPreviewFrame { background: #1b1b1d; border: 1px solid #1b1b1d; }
			QPlainTextEdit#MaterialLogView {
				background: #2b2c27;
				color: #9d9995;
				border: none;
				selection-background-color: #494257;
			}
			QPlainTextEdit#MaterialLogView:hover {
				background: #2b2c27;
				border: none;
			}
			QSplitter::handle { background: #2b2b2d; }
			QSplitter::handle:hover { background: #268bd2; }
		)"));
	}

	void RefreshFileList()
	{
		m_filePicker->clear();
		m_relativePaths.clear();

		std::error_code error;
		for (const auto& entry : std::filesystem::recursive_directory_iterator(m_shaderRoot, error))
		{
			if (!entry.is_regular_file() || entry.path().extension() != ".ovfx")
			{
				continue;
			}
			m_relativePaths.push_back(std::filesystem::relative(entry.path(), m_shaderRoot).generic_string());
		}
		std::ranges::sort(m_relativePaths);
		for (const std::string& relative : m_relativePaths)
		{
			m_filePicker->addItem(QString::fromStdString(relative));
		}
	}

	void OnFileSelected(int p_row)
	{
		if (p_row < 0 || p_row >= static_cast<int>(m_relativePaths.size()))
		{
			return;
		}
		m_current = m_relativePaths[p_row];
		LoadSelectedSource();
	}

	void LoadSelectedSource()
	{
		if (m_current.empty())
		{
			return;
		}

		std::ifstream stream(m_shaderRoot / m_current, std::ios::binary);
		if (!stream)
		{
			Log("cannot read " + m_current, kLogError);
			SetStatus(kLogError, QStringLiteral("Cannot read the file"));
			return;
		}
		const std::string text{ std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
		m_source->setPlainText(QString::fromStdString(text));
		Log("loaded " + m_current, kLogInfo);
		SetStatus(kStatusIdle, QStringLiteral("Loaded %1").arg(QString::fromStdString(m_current)));

		// The preview renders with the render module's context, which the viewport creates: the
		// first one waits for showEvent (OnFirstShow), the rest follow every file change.
		if (m_ready)
		{
			Preview();
		}
	}

	void Apply()
	{
		if (m_current.empty())
		{
			return;
		}
		Moon::Matc::CompileOptions options;
		options.displayName = std::filesystem::path(m_current).filename().string();
		options.includeDirs = { m_shaderRoot.string() };

		const std::string source = m_source->toPlainText().toStdString();
		const Moon::Matc::CompileResult result = Moon::Matc::CompileSource(source, options);
		if (!result.success || result.passthrough)
		{
			Log(std::string("[MoonMatc] ") + (result.error.empty() ? "not a material (passthrough)" : result.error),
				kLogWarning);
			SetStatus(kLogWarning, QStringLiteral("Compile failed"));
			return;
		}

		// The shader belongs to the resource manager and is reloaded *in place*: every material
		// that was drawing with it - in the scene and in the preview - points at the same Shader
		// object, so replacing its programs switches all of them at once. No rebinding pass.
		auto& context = ::Core::Global::ServiceLocator::Get<::Editor::Core::Context>();
		const std::string shaderKey = ToShaderKey(m_current);
		std::string driverError;
		bool reloaded = false;

		// Shaders are GL objects, and a button callback runs with no context current. The
		// module's context is the one they belong on - it is where the draws that use them
		// happen - so it is made current for the duration of the compile. (The preview widget
		// does the same for its own compiles; the panel still never picks a context itself.)
		auto* host = context.driver != nullptr ? context.driver->GetContextHost() : nullptr;
		if (host == nullptr || !host->MakeCurrent())
		{
			Log("[Shader] the render module has no GL context yet: nothing was reloaded", kLogError);
			SetStatus(kLogError, QStringLiteral("No render context"));
			return;
		}

		Rendering::Resources::Shader* shader = context.shaderManager.GetResource(shaderKey, false);
		if (shader == nullptr)
		{
			shader = Rendering::Resources::Loaders::ShaderLoader::CreateFromPackage(
				result.package.data(), static_cast<uint32_t>(result.package.size()), shaderKey, &driverError);
			if (shader != nullptr)
			{
				context.shaderManager.RegisterResource(shaderKey, shader);
				reloaded = true;
			}
		}
		else
		{
			reloaded = Rendering::Resources::Loaders::ShaderLoader::RecompileFromPackage(
				*shader, result.package.data(), static_cast<uint32_t>(result.package.size()), &driverError);
		}

		host->DoneCurrent();

		if (!reloaded)
		{
			Log("[Shader] " + (driverError.empty() ? std::string("the driver refused the material") : driverError),
				kLogError);
			SetStatus(kLogError, QStringLiteral("The driver refused the material"));
			return;
		}

		Log(m_current + " reloaded (" + std::to_string(result.variantCount)
			+ " variant(s), " + std::to_string(result.package.size()) + " bytes)", kLogSuccess);
		SetStatus(kLogSuccess, QStringLiteral("Reloaded %1").arg(QString::fromStdString(m_current)));
		Preview();
	}

	void Preview()
	{
		if (!m_ready)
		{
			// The preview widget needs the render module's context, which the viewport creates;
			// it waits for that (OnFirstShow).
			return;
		}
		if (m_current.empty() || m_preview == nullptr)
		{
			return;
		}

		// Compile what is in the editor - the same source Apply would compile - and hand the
		// package to the preview widget. The widget builds the Shader itself, on the module's
		// context; the panel still never touches GL.
		Moon::Matc::CompileOptions options;
		options.displayName = std::filesystem::path(m_current).filename().string();
		options.includeDirs = { m_shaderRoot.string() };

		const Moon::Matc::CompileResult result = Moon::Matc::CompileSource(
			m_source->toPlainText().toStdString(), options);
		if (!result.success || result.passthrough)
		{
			Log(std::string("[MoonMatc] ") + (result.error.empty() ? "not a material (passthrough)" : result.error),
				kLogWarning);
			SetStatus(kLogWarning, QStringLiteral("Compile failed"));
			m_preview->SetShaderPackage({}, {});
			return;
		}

		m_preview->SetShaderPackage(result.package, ToShaderKey(m_current));
	}

	/** The first time the panel is shown: compile the source the file picker landed on and hand
	 * it to the preview widget, which builds the shader on its next paint (with the render
	 * module's context current). */
	void OnFirstShow()
	{
		if (m_ready)
		{
			return;
		}
		m_ready = true;
		Preview();
	}

	void SetStatus(const QColor& p_color, const QString& p_tooltip)
	{
		m_status->setStyleSheet(
			QStringLiteral("background: %1; border-radius: 4px;").arg(p_color.name()));
		m_status->setToolTip(p_tooltip);
	}

	void Log(const std::string& p_text, const QColor& p_color = kLogInfo)
	{
		QTextCursor cursor(m_log->document());
		cursor.movePosition(QTextCursor::End);
		QTextCharFormat format;
		format.setForeground(p_color);
		cursor.setCharFormat(format);
		cursor.insertText(QString::fromStdString(p_text) + QLatin1Char('\n'));
		m_log->verticalScrollBar()->setValue(m_log->verticalScrollBar()->maximum());

		// Also to stdout: that is where a shader compile failure shows up too, and it is what a
		// scripted run greps for. (Not CORE_INFO: the panel is built while the editor's panels
		// are still being constructed, and the logger's panel sink is not ready for it.)
		std::cout << "[MaterialEditor] " << p_text << std::endl;
	}

	MaterialEditorPanel* m_owner = nullptr;
	std::filesystem::path m_shaderRoot;
	std::vector<std::string> m_relativePaths;
	std::string m_current;
	bool m_ready = false;

	QFrame* m_status = nullptr;
	QComboBox* m_filePicker = nullptr;
	QToolButton* m_apply = nullptr;
	QToolButton* m_reload = nullptr;
	QToolButton* m_clear = nullptr;
	QLabel* m_position = nullptr;
	/** The surface the preview view is shown on: it renders on the module's context and pastes. */
	::MOON::MaterialPreviewWidget* m_preview = nullptr;
	::MOON::MaterialCodeEditor* m_source = nullptr;
	QPlainTextEdit* m_log = nullptr;
};

MOON::MaterialEditorPanel::MaterialEditorPanel(QWidget* p_parent)
	: QDockWidget(p_parent)
	, mInternal(new MaterialEditorPanelInternal(this))
{
	// The View menu toggles this panel through the service locator, like the other docks.
	RegService(MaterialEditorPanel, *this);
}

void MOON::MaterialEditorPanel::showEvent(QShowEvent* p_event)
{
	QDockWidget::showEvent(p_event);
	if (mInternal != nullptr)
	{
		mInternal->OnFirstShow();
	}
}

MOON::MaterialEditorPanel::~MaterialEditorPanel()
{
	delete mInternal;
}
