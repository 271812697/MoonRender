#include "DocumentSession.h"

#include "editor/parsescene.h"
#include "feature/MoonDocument.h"

#include <QtCore/Qt>

namespace MOON
{
	DocumentSession::DocumentSession(QObject* p_parent) : QObject(p_parent)
	{
	}

	void DocumentSession::SetRevealWorkspace(std::function<void()> p_reveal)
	{
		m_revealWorkspace = std::move(p_reveal);
	}

	void DocumentSession::Open(const QString& p_path)
	{
		// The request is recorded first, then the workspace is brought up: raising the
		// workspace is what makes the viewport paint, and a paint that lands while that is
		// happening must already see the request.
		m_pendingPath = p_path;
		m_readPending = true;
		if (m_revealWorkspace)
		{
			m_revealWorkspace();
		}
	}

	const QString& DocumentSession::Path() const
	{
		return m_path;
	}

	void DocumentSession::Clear()
	{
		m_path.clear();
		// A read that was asked for but has not happened yet belongs to the document that
		// is being closed: leaving it pending would read it into the next one, or - when
		// only the path had been cleared - read an empty one.
		m_pendingPath.clear();
		m_readPending = false;
	}

	DocumentSession::ReadResult DocumentSession::FlushPendingRead()
	{
		ReadResult result;
		if (!m_readPending)
		{
			return result;
		}

		// Taken before the read: everything the read itself may trigger (a feature chain
		// that opens a document of its own, a slot that closes the one being read) must
		// leave this frame with a clean slate, not with a request it can schedule again.
		const QString path = m_pendingPath;
		m_pendingPath.clear();
		m_readPending = false;
		if (path.isEmpty())
		{
			return result;
		}

		const std::string readPath = path.toUtf8().toStdString();
		if (path.endsWith(QString(".") + MoonDocument::extension(), Qt::CaseInsensitive))
		{
			// A .moon file is a feature chain, not a mesh: it is read by rebuilding the body
			// instead of by the scene parser.
			result.read = MoonDocument::open(readPath);
			result.refreshTree = result.read;
		}
		else
		{
			OpenSceneFile(readPath);
			result.read = true;
		}

		if (!result.read)
		{
			return result;
		}

		m_path = path;
		// The document is up: whoever keeps the recent projects hears about it here, where
		// the read really happened.
		emit documentOpened(path);
		return result;
	}
}
