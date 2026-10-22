#pragma once
#include <QAction>
#include <QObject>
namespace MOON
{
	class Command : public QObject
	{
		Q_OBJECT
	public:
		Command(QObject* parent);
		virtual ~Command()=default ;
		virtual void execute() = 0;
		QAction* action() const { return mAction; }
		/** Icon that has a separate picture for the checked state (the solid variant of a
		 * ribbon icon). The single-path overload sets one file for one state. */
		void setIcon(const QString& p_offPath, const QString& p_onPath);
		void setIcon(const QString& path,bool on=true);
		virtual bool getEnabledStatus() const { return true; }
		void setAction(QAction* action);
	private:
		QAction* mAction = nullptr;
	};
}


