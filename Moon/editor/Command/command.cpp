#include "command.h"
namespace MOON {
	//-----------------------------------------------------------------------------
	Command::Command(QObject* parent)
		: QObject(parent)
	{	
	}
	void Command::setIcon(const QString& path, bool on )
	{
		if (mAction) {
			QIcon icon;
			icon.addFile(path, QSize(), QIcon::Normal, on?QIcon::On: QIcon::Off);
			
			mAction->setIcon(icon);
		}
	}
	void Command::setIcon(const QString& p_offPath, const QString& p_onPath)
	{
		if (mAction) {
			QIcon icon;
			icon.addFile(p_offPath, QSize(), QIcon::Normal, QIcon::Off);
			icon.addFile(p_onPath, QSize(), QIcon::Normal, QIcon::On);
			mAction->setIcon(icon);
		}
	}
	void Command::setAction(QAction* action)
	{
		mAction = action;
		QObject::connect(action, &QAction::triggered, this, &Command::execute);
	}
}


