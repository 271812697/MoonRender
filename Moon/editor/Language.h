#pragma once

#include <QString>

class QAction;
class QApplication;
class QWidget;

namespace MOON
{
	/** Chinese/English UI language.
	 *
	 * The toolbars already carry both names for every command — setLabel(action, "水平",
	 * "Horizontal") was there from the start, used for the tooltip — so the language is mostly a
	 * matter of deciding which of the two goes on the button. Group captions, menu titles and the
	 * ribbon page tabs only had one string; those are looked up in a small dictionary.
	 *
	 * The switch applies in place: Language::Apply() walks every action and label that remembered
	 * both spellings and rewrites them. */
	namespace Language
	{
		enum class ELanguage
		{
			Chinese,
			English
		};

		/** Reads the stored choice (QSettings "ui/language"); MOON_LANG=zh|en overrides it, which
		 * is how a screenshot in the other language is taken. */
		void Initialize();
		ELanguage Current();
		void Set(ELanguage p_language, bool p_persist = true);

		/** The English spelling of a Chinese label, or the Chinese one when it is unknown. */
		QString English(const QString& p_chinese);

		/** Applies p_zh or p_en according to the current language. */
		QString Pick(const QString& p_zh, const QString& p_en);

		/** Chinese for an English label written in the code (the feature panels are authored in
		 * English: Length, Radius, Offset ...). Returns the input when it has no translation, so
		 * an unknown parameter still shows something. */
		QString Localize(const QString& p_english);

		/** Sets an action's label for the current language and remembers both spellings so a
		 * later switch can rewrite it (this is what the toolbars' setLabel helpers call). */
		void SetLabel(QAction* p_action, const QString& p_zh, const QString& p_en);

		/** The same for a plain widget with a text (a button): both spellings are remembered and
		 * Apply() rewrites it on a switch. */
		void SetText(QWidget* p_widget, const QString& p_zh, const QString& p_en);

		/** Window and dock titles: a dock's title is drawn by its title bar widget, not by an
		 * action, so it needs its own remember-and-rewrite path. */
		void SetWindowTitle(QWidget* p_widget, const QString& p_zh, const QString& p_en);

		/** Rewrites every action, caption and tab that remembered both spellings. */
		void Apply(QApplication* p_app);
	}
}
