#pragma once
#include <QFont>
#include <QPlainTextEdit>

class QEvent;
class QPaintEvent;
class QRect;
class QResizeEvent;
class QSyntaxHighlighter;

namespace MOON
{
	/** The source editor of the material panel.
	 *
	 * Qt's plain text edit plus what a source file needs, in the shape Inviwo's code editor has
	 * it: a line-number gutter that scrolls with the text, the current line marked, Tab indenting
	 * (Shift+Tab unindenting) four spaces instead of moving the focus, no wrapping, and a Monokai
	 * palette for the .ovfx material DSL and the GLSL inside it.
	 *
	 * The colours are the editor's own and not the application's: source reads best on the
	 * darker, slightly warm background Inviwo uses for code, whatever the panels around it look
	 * like. */
	class MaterialCodeEditor : public QPlainTextEdit
	{
		Q_OBJECT
	public:
		explicit MaterialCodeEditor(QWidget* p_parent = nullptr);

		/** The monospace font the editor picked, so the panel's output view can match it. */
		static QFont CodeFont();

	protected:
		bool event(QEvent* p_event) override;
		void resizeEvent(QResizeEvent* p_event) override;

	private:
		class LineNumberArea;

		void PaintLineNumbers(QPaintEvent* p_event);
		int LineNumberAreaWidth() const;
		void UpdateLineNumberAreaWidth();
		void UpdateLineNumberArea(const QRect& p_rect, int p_dy);
		void HighlightCurrentLine();

		LineNumberArea* m_lineNumbers = nullptr;
		QSyntaxHighlighter* m_highlighter = nullptr;
	};
}
