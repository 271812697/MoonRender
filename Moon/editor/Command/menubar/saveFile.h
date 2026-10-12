#pragma once
#include "editor/Command/command.h"
#include <QString>

namespace MOON {

	/** Writes the body's feature chain to a .moon document.
	 *
	 * Save writes to the path the document was last saved under, Save As asks for a new
	 * one; a document that was never written asks for a path either way, so there is no
	 * "saved nowhere" state to explain. */
	class SaveFileCommand : public Command
	{
		Q_OBJECT

	public:
		SaveFileCommand(QObject* parent, bool p_saveAs);
		virtual void execute() override;
		/** Where the document was written last, empty while it was never written: the
		 * start page remembers it, and closing a project forgets it. */
		static const QString& CurrentPath() { return s_path; }
		static void ResetPath() { s_path.clear(); }

	private:
		bool m_saveAs = false;
		/** Where the document was written last. Both commands share it, which is what
		 * makes Save follow a Save As. */
		static QString s_path;
	};

}
