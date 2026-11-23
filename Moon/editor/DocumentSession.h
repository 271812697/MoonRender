#pragma once
#include <QObject>
#include <QString>

#include <functional>

namespace MOON
{
	/** The document the viewport has open: the file it was read from, and the read itself.
	 *
	 * Opening is a request, not an action. The caller says which file; the read happens a
	 * frame later, in the frame loop, where the scene, the view and the GL context exist and
	 * where building a feature chain is allowed. A file named at start-up, chosen on the
	 * start page, picked in the File menu or handed on the command line all take this road -
	 * which is why none of them can be read twice, and why none of them can be read before
	 * there is a viewport to read into.
	 *
	 * It is a QObject for the one thing the rest of the editor needs from it: knowing when a
	 * document came up (the recent projects list is written then).
	 */
	class DocumentSession final : public QObject
	{
		Q_OBJECT
	public:
		explicit DocumentSession(QObject* p_parent = nullptr);

		/** What showing the workspace is for the caller. The session asks for it before a
		 * read, because a viewport that is not shown does not paint and the read happens in
		 * a paint. The editor sets this once, when it has a workspace to show. */
		void SetRevealWorkspace(std::function<void()> p_reveal);

		/** Asks for p_path to be read into the scene. The read itself happens in the next
		 * frame (see FlushPendingRead). */
		void Open(const QString& p_path);

		/** The file the open document was read from: empty while nothing is open, and still
		 * the previous document while a read is pending. */
		const QString& Path() const;

		/** Forgets the document - and a read that is still pending with it. Called when the
		 * document that was open is closed, so nothing that looks at it next (the picture
		 * taken on the way out, a card on the start page) mistakes a closed document for an
		 * open one. */
		void Clear();

		/** What a pending read produced. */
		struct ReadResult
		{
			/** The file was read and the scene changed. */
			bool read = false;
			/** The tree has to be rebuilt from the scene, not merely updated after it. */
			bool refreshTree = false;
		};

		/** Reads the pending file, if there is one. Called from the frame loop: that is
		 * where the scene, the view and the GL context are valid, and where building a
		 * feature chain is allowed. Emits documentOpened on the way. */
		ReadResult FlushPendingRead();

	signals:
		/** A document was read into the scene; p_path is the file it came from. Emitted
		 * where the read happened, in the frame loop. */
		void documentOpened(const QString& p_path);

	private:
		std::function<void()> m_revealWorkspace;
		QString m_path;
		QString m_pendingPath;
		bool m_readPending = false;
	};
}
