#pragma once

#include <QWidget>

class QStackedWidget;

namespace MOON
{
	/** The page the application is shown on before a document is.
	 *
	 * It carries the two things a session starts with - make a project, or open one -
	 * and the projects that were opened last as cards with a picture of each. Picking
	 * one asks for it to be opened; the panel opens nothing itself, the request goes to
	 * the command that owns opening a document, and the workspace is what is shown
	 * once that worked.
	 *
	 * It is also what a document is closed back onto, so a scene can be left without
	 * leaving the application. */
	class HomePanel : public QWidget
	{
		Q_OBJECT
	public:
		explicit HomePanel(QWidget* p_parent = nullptr);
		~HomePanel() override;

		/** The stack the start page and the workspace are the pages of, and which page
		 * of it is the workspace. Choosing a project is what takes the workspace's
		 * place, so while the start page is up there is nothing else on screen. */
		void AttachWorkspace(QStackedWidget* p_stack, int p_workspaceIndex);

		/** Shows the start page, with the recent projects read again. */
		void ShowStartPage();
		/** Shows the workspace (the viewport and the panels around it). */
		void ShowWorkspace();
		/** Reads the recent projects again, keeping the page as it is. */
		void RefreshRecentProjects();

	signals:
		/** "New Project" was pressed. */
		void newProjectRequested();
		/** "Open Project" was pressed: opening is asked for rather than done here, so
		 * the start page and the File menu take the same path. */
		void openProjectRequested();
		/** The card of p_path was activated. */
		void projectRequested(const QString& p_path);
		/** The card of p_path was taken off the list. */
		void projectForgotten(const QString& p_path);

	private:
		class Internal;
		Internal* mInternal = nullptr;
	};
}
