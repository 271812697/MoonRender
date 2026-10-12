#pragma once

#include <QDateTime>
#include <QImage>
#include <QPixmap>
#include <QString>
#include <vector>

namespace MOON
{
	/** One project of the "recent" list the start page shows. */
	struct RecentProject
	{
		QString path;
		QString name;
		QDateTime lastOpened;
		/** The picture of the project (its viewport as it was when it was last
		 * written), null when there is none yet - the card falls back to the icon of
		 * the file then. */
		QPixmap preview;
	};

	/** The projects that were opened last.
	 *
	 * A small list kept in the application settings, with a picture of each one next
	 * to the settings file: the start page shows them as cards, and a project whose
	 * file is gone is dropped when the list is read back. Nothing here depends on the
	 * document, so the list survives a document that cannot be read. */
	class RecentProjects
	{
	public:
		/** How many projects are kept: more than a start page shows at once on
		 * purpose, so removing one still leaves a page full. */
		static constexpr int kMaxCount = 12;

		/** The list, most recently opened first. Entries whose file is no longer there
		 * are left out (and taken out of the settings). */
		static std::vector<RecentProject> Load();

		/** Puts p_path at the top of the list with the time it is opened, and keeps
		 * p_preview as its picture when one is given. A path that is not a file, or
		 * that is already the first entry with a picture, is left as it is. */
		static void Remember(const QString& p_path, const QImage& p_preview = QImage());

		/** Takes p_path out of the list (its picture with it). */
		static void Forget(const QString& p_path);

		/** Where the picture of p_path is kept. */
		static QString PreviewFilePath(const QString& p_path);

		/** A picture of the 3D viewport, sized for a card - the picture that is put
		 * on a project when it is written. Empty when the view runs nothing. */
		static QImage GrabViewportPreview();
	};
}
