#include "MaterialCodeEditor.h"

#include <QFontDatabase>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QPainter>
#include <QRect>
#include <QRegularExpression>
#include <QStringList>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextFormat>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace
{
	// The code palette. These are Inviwo's editor colours (Monokai): the application's dark theme
	// is neutral grey, the editor's dark is slightly warm, so a source file reads as its own
	// surface and not as a styled text box.
	const QColor kBackground(0x2b, 0x2c, 0x27);
	const QColor kText(0xf8, 0xf8, 0xf2);
	const QColor kLineHighlight(0x21, 0x22, 0x1d);
	const QColor kSelection(0x49, 0x42, 0x57);
	const QColor kLineNumber(0x75, 0x76, 0x6f);
	const QColor kLineNumberCurrent(0xd8, 0xd8, 0xd0);
	const QColor kGutterLine(0x3a, 0x3b, 0x35);
	const QColor kKeyword(0x66, 0xd9, 0xef);        // GLSL qualifiers and the DSL blocks
	const QColor kType(0xa6, 0xe2, 0x2e);           // GLSL and DSL types
	const QColor kFunction(0x66, 0xd9, 0xef);       // built-in functions
	const QColor kBuiltinVariable(0xfd, 0x97, 0x1f); // gl_* variables and DSL attributes
	const QColor kPreprocessor(0xf9, 0x26, 0x72);
	const QColor kLiteral(0xff, 0xd5, 0x69);
	const QColor kConstant(0xae, 0x81, 0xff);       // numbers and ALL_CAPS feature names
	const QColor kComment(0x88, 0x88, 0x88);
	const QColor kEntryPoint(0xa6, 0xe4, 0x28);     // main

	constexpr int kIndentWidth = 4;

	QTextCharFormat Format(const QColor& p_color, bool p_bold = false)
	{
		QTextCharFormat format;
		format.setForeground(p_color);
		// No background: the editor's own background shows through, and the current-line
		// selection can be painted behind the text instead of being covered by a format.
		if (p_bold)
		{
			format.setFontWeight(QFont::Bold);
		}
		return format;
	}

	/** The material DSL and the GLSL inside its stages.
	 *
	 * Rule order is the order Inviwo applies its GLSL highlighter in: types, qualifiers,
	 * built-ins, preprocessor, literals, comments. Later rules overwrite earlier ones, so a
	 * keyword inside a string or a comment does not win over it. The material words (material,
	 * vertex, parameters, ...) are added in front of literals because they are the structure of
	 * the file rather than GLSL. */
	class MaterialHighlighter final : public QSyntaxHighlighter
	{
	public:
		explicit MaterialHighlighter(QTextDocument* p_document) :
			QSyntaxHighlighter(p_document),
			m_default(Format(kText)),
			m_comment(Format(kComment)),
			m_commentBegin(QStringLiteral("\\/\\*")),
			m_commentEnd(QStringLiteral("\\*\\/"))
		{
			BuildRules();
		}

	protected:
		void highlightBlock(const QString& p_text) override
		{
			setFormat(0, static_cast<int>(p_text.length()), m_default);

			for (const Rule& rule : m_rules)
			{
				auto matches = rule.pattern.globalMatch(p_text);
				while (matches.hasNext())
				{
					const QRegularExpressionMatch match = matches.next();
					setFormat(static_cast<int>(match.capturedStart()),
						static_cast<int>(match.capturedLength()), rule.format);
				}
			}

			// Block comments carry their state across lines: an unterminated /* keeps the blocks
			// after it commented until it is closed.
			setCurrentBlockState(0);
			int start = -1;
			if (previousBlockState() == 1)
			{
				start = 0;
			}
			else
			{
				const QRegularExpressionMatch begin = m_commentBegin.match(p_text);
				if (begin.hasMatch())
				{
					start = static_cast<int>(begin.capturedStart());
				}
			}

			while (start >= 0)
			{
				const QRegularExpressionMatch end = m_commentEnd.match(p_text, start);
				int length = 0;
				if (end.hasMatch())
				{
					length = static_cast<int>(end.capturedEnd()) - start;
				}
				else
				{
					setCurrentBlockState(1);
					length = static_cast<int>(p_text.length()) - start;
				}
				setFormat(start, length, m_comment);

				const QRegularExpressionMatch next = m_commentBegin.match(p_text, start + length);
				start = next.hasMatch() ? static_cast<int>(next.capturedStart()) : -1;
			}
		}

	private:
		struct Rule
		{
			QRegularExpression pattern;
			QTextCharFormat format;
		};

		void AddPattern(const QTextCharFormat& p_format, const QString& p_pattern)
		{
			m_rules.push_back(Rule{ QRegularExpression(p_pattern), p_format });
		}

		void AddWords(const QTextCharFormat& p_format, std::initializer_list<const char*> p_words)
		{
			for (const char* word : p_words)
			{
				AddPattern(p_format,
					QStringLiteral("\\b%1\\b").arg(QString::fromLatin1(word)));
			}
		}

		void BuildRules()
		{
			const QTextCharFormat type = Format(kType);
			const QTextCharFormat keyword = Format(kKeyword);
			const QTextCharFormat function = Format(kFunction);
			const QTextCharFormat builtin = Format(kBuiltinVariable);
			const QTextCharFormat preprocessor = Format(kPreprocessor);
			const QTextCharFormat literal = Format(kLiteral);
			const QTextCharFormat constant = Format(kConstant);
			const QTextCharFormat entry = Format(kEntryPoint);
			const QTextCharFormat block = Format(kKeyword, true);

			// GLSL types (the same list Inviwo's GLSL highlighter uses), plus the DSL's own
			// type names.
			AddWords(type, { "double", "float", "uint", "int", "bool", "void", "mat[2-4]",
				"[biud]?vec[2-4]", "float[2-4]", "int[2-4]", "u?sampler[1-3]D",
				"sampler[1-2]DArray", "sampler2DRect", "u?samplerCube", "samplerCubeArray",
				"samplerBuffer", "sampler2DMS", "sampler2DMSArray", "sampler[1-2]DShadow",
				"samplerCubeShadow", "sampler2DRectShadow", "sampler[1-2]DArrayShadow" });

			AddWords(keyword, { "struct", "uniform", "attribute", "varying", "in", "out", "inout",
				"const", "discard", "if", "else", "while", "for", "continue", "break", "return",
				"layout", "flat", "smooth", "noperspective", "centroid", "precision", "highp",
				"mediump", "lowp", "true", "false", "defined" });

			AddWords(builtin, { "gl_Position", "gl_PointSize", "gl_FragCoord", "gl_FragColor",
				"gl_FragData", "gl_FragDepth", "gl_FrontFacing", "gl_PointCoord", "gl_VertexID",
				"gl_InstanceID", "gl_PrimitiveID", "gl_Layer", "gl_ViewportIndex" });

			AddWords(function, { "sin", "cos", "tan", "asin", "acos", "atan", "radians", "degrees",
				"pow", "exp", "exp2", "log", "log2", "sqrt", "inversesqrt", "abs", "sign", "floor",
				"ceil", "fract", "mod", "min", "max", "clamp", "mix", "step", "smoothstep", "length",
				"distance", "dot", "cross", "normalize", "faceforward", "reflect", "refract",
				"texture", "textureLod", "textureQueryLevels", "textureQueryLod", "textureSize",
				"texelFetch", "sample", "dFdx", "dFdy", "fwidth", "transpose", "inverse",
				"determinant", "matrixCompMult", "any", "all", "not", "lessThan", "greaterThan",
				"equal", "notEqual" });

			AddWords(entry, { "main" });

			// Preprocessor lines: the directive word only, so its operands keep their own colour.
			for (const char* directive : { "define", "include", "if", "ifdef", "ifndef", "else",
				"elif", "endif", "error", "pragma", "line", "version" })
			{
				AddPattern(preprocessor,
					QStringLiteral("#[ \\t]*%1").arg(QString::fromLatin1(directive)));
			}
			AddWords(preprocessor, { "__LINE__", "__FILE__", "__VERSION__" });

			// The material DSL around the GLSL: the three stages and the parameter list are the
			// structure of the file, so they are the bold blue; the attributes are orange like
			// built-in variables.
			AddWords(block, { "material", "vertex", "fragment", "geometry" });
			AddWords(keyword, { "parameters", "features", "passes", "variantFilter" });
			AddWords(builtin, { "name", "shadingModel", "blending", "culling", "depthWrite",
				"type", "default" });

			// Feature names are ALL_CAPS in the DSL (WITH_EDGE, VERTEX_POS_NORMAL, ...): they are
			// constants to the reader, like numbers.
			AddPattern(constant, QStringLiteral("\\b[A-Z][A-Z0-9_]{2,}\\b"));

			AddPattern(constant, QStringLiteral("\\b0[xX][0-9a-fA-F]+[uU]?\\b"));
			AddPattern(constant, QStringLiteral("\\b([0-9]+\\.)?[0-9]+([eE][+-]?[0-9]+)?[fFuU]?\\b"));

			AddPattern(literal, QStringLiteral("\"([^\"\\\\]|\\\\.)*\""));

			AddPattern(Format(kComment), QStringLiteral("\\/\\/.*$"));
		}

		QTextCharFormat m_default;
		QTextCharFormat m_comment;
		QRegularExpression m_commentBegin;
		QRegularExpression m_commentEnd;
		std::vector<Rule> m_rules;
	};

	int LeadingSpaces(const QString& p_line)
	{
		int spaces = 0;
		while (spaces < kIndentWidth && spaces < p_line.size() &&
			p_line.at(spaces) == QLatin1Char(' '))
		{
			++spaces;
		}
		return spaces;
	}

	/** Indents the selected lines (or inserts one indent at the cursor), keeping the selection
	 * around the same text. */
	void InsertIndent(QTextCursor p_cursor)
	{
		if (!p_cursor.hasSelection())
		{
			p_cursor.insertText(QString(kIndentWidth, QLatin1Char(' ')));
			return;
		}

		const int start = p_cursor.position();
		const int end = p_cursor.anchor();

		p_cursor.setPosition(start, QTextCursor::MoveAnchor);
		const int startBlock = p_cursor.block().blockNumber();
		p_cursor.setPosition(end, QTextCursor::MoveAnchor);
		const int endBlock = p_cursor.block().blockNumber();

		const int lineCount = std::abs(endBlock - startBlock) + 1;
		const QTextCursor::MoveOperation direction =
			start < end ? QTextCursor::PreviousBlock : QTextCursor::NextBlock;

		p_cursor.movePosition(QTextCursor::StartOfBlock);
		for (int i = 0; i < lineCount; ++i)
		{
			p_cursor.insertText(QString(kIndentWidth, QLatin1Char(' ')));
			p_cursor.movePosition(direction);
		}

		if (start > end)
		{
			p_cursor.setPosition(end + kIndentWidth, QTextCursor::MoveAnchor);
			p_cursor.setPosition(start + kIndentWidth * lineCount, QTextCursor::KeepAnchor);
		}
		else
		{
			p_cursor.setPosition(end + kIndentWidth * lineCount, QTextCursor::MoveAnchor);
			p_cursor.setPosition(start + kIndentWidth, QTextCursor::KeepAnchor);
		}
	}

	/** Removes up to one indent from the selected lines (or from in front of the cursor). */
	void RemoveIndent(QTextCursor p_cursor, QPlainTextEdit* p_editor)
	{
		if (p_cursor.hasSelection())
		{
			const int start = p_cursor.position();
			const int end = p_cursor.anchor();

			if (start > end)
			{
				p_cursor.setPosition(end);
				p_cursor.movePosition(QTextCursor::StartOfBlock);
				p_cursor.setPosition(start, QTextCursor::KeepAnchor);
				p_cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
			}
			else
			{
				p_cursor.setPosition(end);
				p_cursor.movePosition(QTextCursor::EndOfBlock);
				p_cursor.setPosition(start, QTextCursor::KeepAnchor);
				p_cursor.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
			}

			QStringList lines = p_cursor.selectedText()
				.replace(QChar(QChar::ParagraphSeparator), QLatin1Char('\n'))
				.split(QLatin1Char('\n'));
			const int removedFromFirstLine = lines.isEmpty() ? 0 : LeadingSpaces(lines.front());

			int removed = 0;
			for (QString& line : lines)
			{
				const int spaces = LeadingSpaces(line);
				line.remove(0, spaces);
				removed += spaces;
			}

			p_cursor.insertText(lines.join(QLatin1Char('\n')));

			if (start > end)
			{
				p_cursor.setPosition(end - removedFromFirstLine, QTextCursor::MoveAnchor);
				p_cursor.setPosition(start - removed, QTextCursor::KeepAnchor);
			}
			else
			{
				p_cursor.setPosition(end - removed, QTextCursor::MoveAnchor);
				p_cursor.setPosition(start - removedFromFirstLine, QTextCursor::KeepAnchor);
			}
			p_editor->setTextCursor(p_cursor);
			return;
		}

		const int start = p_cursor.position();
		p_cursor.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, kIndentWidth);
		const QString text = p_cursor.selectedText();
		int spaces = text.size();
		while (spaces > 0 && text.at(spaces - 1) != QLatin1Char(' '))
		{
			--spaces;
		}
		if (spaces > 0)
		{
			p_cursor.setPosition(start - spaces, QTextCursor::KeepAnchor);
			p_cursor.removeSelectedText();
		}
	}
}

QFont MOON::MaterialCodeEditor::CodeFont()
{
	// A monospace font the platform actually has. The list is what editors ship with on the
	// three platforms; the system fixed font is the fallback.
	const QStringList preferred{
		QStringLiteral("Cascadia Mono"),
		QStringLiteral("Consolas"),
		QStringLiteral("JetBrains Mono"),
		QStringLiteral("Fira Code"),
		QStringLiteral("Source Code Pro"),
		QStringLiteral("DejaVu Sans Mono") };

	const QStringList available = QFontDatabase().families();
	for (const QString& family : preferred)
	{
		if (available.contains(family))
		{
			QFont font(family);
			font.setPointSize(10);
			font.setStyleHint(QFont::Monospace);
			font.setFixedPitch(true);
			return font;
		}
	}

	QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
	font.setPointSize(10);
	return font;
}

class MOON::MaterialCodeEditor::LineNumberArea : public QWidget
{
public:
	explicit LineNumberArea(MaterialCodeEditor* p_editor) :
		QWidget(p_editor),
		m_editor(p_editor)
	{
	}

	QSize sizeHint() const override
	{
		return QSize(m_editor->LineNumberAreaWidth(), 0);
	}

protected:
	void paintEvent(QPaintEvent* p_event) override
	{
		m_editor->PaintLineNumbers(p_event);
	}

private:
	MaterialCodeEditor* m_editor;
};

MOON::MaterialCodeEditor::MaterialCodeEditor(QWidget* p_parent) :
	QPlainTextEdit(p_parent),
	m_lineNumbers(nullptr),
	m_highlighter(nullptr)
{
	setObjectName("MaterialCodeEditor");
	setLineWrapMode(QPlainTextEdit::NoWrap);
	setWordWrapMode(QTextOption::NoWrap);
	setFrameShape(QFrame::NoFrame);
	setUndoRedoEnabled(true);

	const QFont codeFont = CodeFont();
	setFont(codeFont);
	document()->setDocumentMargin(6.0);
	setTabStopDistance(QFontMetricsF(codeFont).horizontalAdvance(QLatin1Char(' ')) * kIndentWidth);

	// The stylesheet is what paints the viewport; the palette is the fallback for the parts Qt
	// draws itself (the caret, the selection) and for a platform that ignores one of the two.
	// The selector is the qualified class name with Qt's namespace separator: a plain
	// "MaterialCodeEditor" would not match MOON::MaterialCodeEditor.
	QPalette editorPalette = palette();
	editorPalette.setColor(QPalette::Base, kBackground);
	editorPalette.setColor(QPalette::Text, kText);
	editorPalette.setColor(QPalette::Highlight, kSelection);
	editorPalette.setColor(QPalette::HighlightedText, kText);
	setPalette(editorPalette);
	setStyleSheet(QString::fromLatin1(
		"MOON--MaterialCodeEditor, MOON--MaterialCodeEditor:hover, MOON--MaterialCodeEditor:focus {"
		" background-color: #2b2c27; color: #f8f8f2; border: none;"
		" selection-background-color: #494257; selection-color: #f8f8f2; }"));

	m_lineNumbers = new LineNumberArea(this);
	m_lineNumbers->setFont(codeFont);

	m_highlighter = new MaterialHighlighter(document());

	connect(this, &QPlainTextEdit::blockCountChanged, this,
		[this](int) { UpdateLineNumberAreaWidth(); });
	connect(this, &QPlainTextEdit::updateRequest, this,
		&MaterialCodeEditor::UpdateLineNumberArea);
	connect(this, &QPlainTextEdit::cursorPositionChanged, this,
		&MaterialCodeEditor::HighlightCurrentLine);

	UpdateLineNumberAreaWidth();
	HighlightCurrentLine();
}

bool MOON::MaterialCodeEditor::event(QEvent* p_event)
{
	// Tab belongs to the text, not to the focus chain: it indents the selection (or inserts one
	// indent) and Shift+Tab takes it back out.
	if (p_event->type() == QEvent::KeyPress)
	{
		auto* keyEvent = static_cast<QKeyEvent*>(p_event);
		const bool unmodified =
			(keyEvent->modifiers() & (Qt::ControlModifier | Qt::AltModifier)) == 0;

		if (unmodified &&
			(keyEvent->key() == Qt::Key_Backtab ||
				(keyEvent->key() == Qt::Key_Tab && (keyEvent->modifiers() & Qt::ShiftModifier))))
		{
			keyEvent->accept();
			RemoveIndent(textCursor(), this);
			return true;
		}

		if (unmodified && keyEvent->key() == Qt::Key_Tab)
		{
			keyEvent->accept();
			InsertIndent(textCursor());
			return true;
		}
	}

	return QPlainTextEdit::event(p_event);
}

void MOON::MaterialCodeEditor::resizeEvent(QResizeEvent* p_event)
{
	QPlainTextEdit::resizeEvent(p_event);

	const QRect area = contentsRect();
	m_lineNumbers->setGeometry(
		QRect(area.left(), area.top(), LineNumberAreaWidth(), area.height()));
}

int MOON::MaterialCodeEditor::LineNumberAreaWidth() const
{
	int digits = 1;
	for (int max = std::max(1, blockCount()); max >= 10; max /= 10)
	{
		++digits;
	}

	return 10 + digits * fontMetrics().horizontalAdvance(QLatin1Char('9'));
}

void MOON::MaterialCodeEditor::UpdateLineNumberAreaWidth()
{
	setViewportMargins(LineNumberAreaWidth(), 0, 0, 0);
}

void MOON::MaterialCodeEditor::UpdateLineNumberArea(const QRect& p_rect, int p_dy)
{
	if (p_dy != 0)
	{
		m_lineNumbers->scroll(0, p_dy);
	}
	else
	{
		m_lineNumbers->update(0, p_rect.y(), m_lineNumbers->width(), p_rect.height());
	}

	if (p_rect.contains(viewport()->rect()))
	{
		UpdateLineNumberAreaWidth();
	}
}

void MOON::MaterialCodeEditor::PaintLineNumbers(QPaintEvent* p_event)
{
	QPainter painter(m_lineNumbers);
	painter.setRenderHint(QPainter::TextAntialiasing);
	painter.fillRect(p_event->rect(), kBackground);

	// A hairline between the gutter and the text, so the numbers read as a column and not as
	// stray text in front of the source.
	painter.setPen(kGutterLine);
	painter.drawLine(m_lineNumbers->width() - 1, p_event->rect().top(),
		m_lineNumbers->width() - 1, p_event->rect().bottom());

	const int currentBlock = textCursor().blockNumber();
	QTextBlock block = firstVisibleBlock();
	int blockNumber = block.blockNumber();
	int top = static_cast<int>(blockBoundingGeometry(block).translated(contentOffset()).top());
	int bottom = top + static_cast<int>(blockBoundingRect(block).height());
	const int numberWidth = m_lineNumbers->width() - 10;
	const int lineHeight = fontMetrics().height();

	while (block.isValid() && top <= p_event->rect().bottom())
	{
		if (block.isVisible() && bottom >= p_event->rect().top())
		{
			painter.setPen(blockNumber == currentBlock ? kLineNumberCurrent : kLineNumber);
			painter.drawText(0, top, numberWidth, lineHeight, Qt::AlignRight,
				QString::number(blockNumber + 1));
		}

		block = block.next();
		top = bottom;
		bottom = top + static_cast<int>(blockBoundingRect(block).height());
		++blockNumber;
	}
}

void MOON::MaterialCodeEditor::HighlightCurrentLine()
{
	QList<QTextEdit::ExtraSelection> selections;
	if (!isReadOnly())
	{
		QTextEdit::ExtraSelection selection;
		selection.format.setBackground(kLineHighlight);
		selection.format.setProperty(QTextFormat::FullWidthSelection, true);
		selection.cursor = textCursor();
		selection.cursor.clearSelection();
		selections.append(selection);
	}
	setExtraSelections(selections);
}
