#include "command.h"
#include "editor/Theme.h"
namespace MOON {
	//-----------------------------------------------------------------------------
	Command::Command(QObject* parent)
		: QObject(parent)
	{	
	}
	void Command::setIcon(const QString& path, bool on )
	{
		if (mAction) {
			// Through Theme::Icon: the SVGs carry two colour tokens (#LINE#, #EDGE#) that have to
			// be resolved for the current scheme. The path is remembered so switching the scheme
			// can re-render every icon in place (see Theme::Apply).
			Q_UNUSED(on);
			mAction->setProperty("moonIcon", path);
			mAction->setIcon(MOON::Theme::Icon(path));
		}
	}
	void Command::setIcon(const QString& p_offPath, const QString& p_onPath)
	{
		if (mAction) {
			QIcon icon = MOON::Theme::Icon(p_offPath);
			const QPixmap onPixmap = MOON::Theme::Icon(p_onPath).pixmap(32, 32);
			icon.addPixmap(onPixmap, QIcon::Normal, QIcon::On);
			mAction->setProperty("moonIcon", p_offPath);
			mAction->setIcon(icon);
		}
	}
	void Command::setAction(QAction* action)
	{
		mAction = action;
		QObject::connect(action, &QAction::triggered, this, &Command::execute);
	}
}


