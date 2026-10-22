#pragma once

class QObject;
class QToolBar;

namespace MOON
{
	/** Adds the sketch constraint groups to p_toolbar, with p_owner as the parent of
	 * the commands.
	 *
	 * The constraints are part of the sketch theme: the sketch ribbon page calls this
	 * to put them beside the drawing tools instead of on a page of their own. */
	void AddConstraintGroups(QToolBar* p_toolbar, QObject* p_owner);
}
