#include "editor/Command/menubar/saveFile.h"

#include "feature/MoonDocument.h"
#include "core/log.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>

namespace MOON {

	QString SaveFileCommand::s_path;

	SaveFileCommand::SaveFileCommand(QObject* parentObject, bool p_saveAs)
		: Command(parentObject), m_saveAs(p_saveAs)
	{
		auto action = new QAction(this);
		setAction(action);
		action->setObjectName(QString::fromUtf8(
			p_saveAs ? "actionFileSaveAs" : "actionFileSave"));
		action->setText(p_saveAs ? "Save &As..." : "&Save");
		action->setStatusTip(
			p_saveAs ? "Save the document under another name" : "Save the document");
		action->setShortcut(QCoreApplication::translate(
			"pqFileMenuBuilder",
			p_saveAs ? "Ctrl+Shift+S" : "Ctrl+S",
			nullptr));
	}

	void SaveFileCommand::execute()
	{
		QString path = m_saveAs ? QString() : s_path;
		if (path.isEmpty()) {
			path = QFileDialog::getSaveFileName(
				nullptr,
				tr("Save Document"),
				s_path.isEmpty() ? QDir::homePath() : s_path,
				tr(MoonDocument::fileFilter()));
			if (path.isEmpty()) {
				return;  // the user changed their mind
			}
			if (QFileInfo(path).suffix().isEmpty()) {
				path += QString(".") + MoonDocument::extension();
			}
		}
		// The document reads the path as UTF-8, which is what Qt works in.
		if (MoonDocument::save(path.toUtf8().toStdString())) {
			s_path = path;
		}
	}
}
