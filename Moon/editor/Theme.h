#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

class QApplication;

namespace MOON
{


	/** The colours one scheme needs, named by role rather than by hue.
	 *
	 * The icons are drawn on a dark plate in the dark scheme and on paper in the light one, so
	 * the two colours they use for "structure" (edges and secondary strokes) are part of the
	 * table too: the same SVG cannot be visible on both backgrounds. */
	struct ThemeTokens
	{
		QColor window;
		QColor surface;
		QColor well;
		QColor border;
		QColor text;
		QColor textDim;
		QColor textDisabled;
		QColor accent;
		QColor accentText;
		QColor separator;

		/** CSS colour strings for the places where a gradient or an alpha wash is needed. */
		QString barTop;
		QString barBottom;
		QString barBorder;
		QString hoverWash;
		QString pressedWash;
		QString checkedWash;
		QString menuSelection;

		/** Icon colours: the strokes that must contrast with the background, and the edges of the
		 * solid shapes in the model icons. */
		QColor iconLine;
		QColor iconEdge;
	};

	namespace Theme
	{
		/** The two colour schemes the editor ships with.
		 *
		 * Everything that has a colour — the application palette, the stylesheet's gradients,
		 * the ribbon's own painting and the SVG icons — reads it from here, so a second scheme
		 * is a table of colours rather than a second set of files. */
		enum class ETheme
		{
			Dark,
			Light
		};
		/** Current scheme; set from QSettings at startup, overridable with MOON_THEME for a
		 * screenshot (dark|light). */
		ETheme Current();
		void Set(ETheme p_theme, bool p_persist = true);
		void Initialize();

		const ThemeTokens& Tokens();

		/** Replaces the @tokens in a stylesheet with the current scheme's colours. */
		QString Style(QString p_css);

		/** An icon whose two theme-dependent colours (#LINE#, #EDGE#) are resolved for the
		 * current scheme. Falls back to the file as-is when the resource has no tokens. */
		QIcon Icon(const QString& p_path);

		/** The stylesheet of one ribbon button, at the given label size. Lives here because its
		 * hover/checked plates are theme colours like everything else. */
		QString RibbonButtonStyle(int p_labelPx);

		/** Re-applies the palette, the stylesheet and every icon that was set through
		 * Theme::Icon (it remembers the path on the action). */
		void Apply(QApplication* p_app);
	}
}
