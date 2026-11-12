#pragma once
#include <cstddef>
#include <memory>
#include <vector>

#include "Interactive/Interactive/EventObject.h"
#include "editor/UI/TaskPanel/TaskSession.h"

namespace Part { class TopoShape; }

namespace MOON
{
	class TaskViewWidget;
	class FeatureInteraction;
	class ExecuteCommand;

	/** One operation, as far as picking is concerned: what it can be started on, and how it is
	 * built and edited once it is. */
	struct TaskOfferRule
	{
		const char* name;
		/** The icon the ribbon shows for it (a qrc path, resolved through Theme::Icon so the SVG
		 * follows the colour scheme). The switcher at the cursor is what it is for. */
		const char* icon;
		/** Whether the picked sub-shape offers this operation. */
		bool (*matches)(const Part::TopoShape& p_picked);
		/** The interaction, in its offered state (handles on the pick, nothing built). */
		std::unique_ptr<FeatureInteraction>(*makeInteraction)();
		/** Its panel, taking the interaction over (see MakePadPanel). */
		TaskSession::PanelFactory panel;
	};

	/** Turns a pick in the viewport into an offer of the operation that can be built on it.
	 *
	 * The picking pass publishes nothing - it answers queries - and SelectWidget is what turns its
	 * answer into a selection event. This listens to that, asks the table below which operation the
	 * picked sub-shape offers, and hands that operation to the TaskSession: handles on the model,
	 * nothing built, no panel. The task begins from the first drag of a handle.
	 *
	 * The table is the whole of the operation list. Adding one means writing its interaction and
	 * its panel factory - the function next to its panel, see MakePadPanel - and adding a row;
	 * nothing in here knows what a pad or a fillet is.
	 */
	class SelectionTaskLauncher
	{
	public:
		SelectionTaskLauncher(TaskSession& p_session, TaskViewWidget& p_host);
		~SelectionTaskLauncher();

		SelectionTaskLauncher(const SelectionTaskLauncher&) = delete;
		SelectionTaskLauncher& operator=(const SelectionTaskLauncher&) = delete;

	private:
		/** SelectionManager's SelectAny: something in the viewport was picked. */
		void onSelectionChanged();
		/** SelectionManager's ClearSelect: the click went to empty space, so nothing is
		 * selected and an offer standing on the old pick has nothing to stand on. */
		void onSelectionCleared();

		/** Asks which of the operations the pick offers is meant: a row of icon buttons at the
		 * cursor, the same icons the ribbon shows.
		 * @return false when the user dismissed it. */
		bool chooseRuleAtCursor(const std::vector<const TaskOfferRule*>& p_rules,
			const TaskOfferRule*& p_chosen);

		TaskSession& m_session;
		TaskViewWidget& m_host;
		/** The registrations, kept whole: AddObserver hands back the tag and the command it
		 * allocated, and the command has to be released by hand when it is removed (see the note
		 * on SubjectHelper). */
		ExecuteCommandPair m_observer{ 0, nullptr };
		ExecuteCommandPair m_clearObserver{ 0, nullptr };
		/** Set while an offer is being set up: the dispatcher restarts its list when an observer
		 * is added during a dispatch, which lands here before the setup has returned. */
		bool m_starting = false;
		/** The pick this has already acted on, for the same reason: the same selection arrives
		 * here again when the dispatch starts over. */
		size_t m_handledPick = 0;
		/** The operation this user picked last for a face and for an edge.
		 *
		 * A pick can offer more than one operation (a face offers a pad and a thickness, an edge a
		 * fillet and a chamfer), and there is no way to tell which one is meant from the geometry
		 * alone. The switcher asks, and the answer is remembered per kind, so the common case stays
		 * a single click: the menu only appears when the pick really has alternatives.
		 */
		const char* m_lastFaceRule = nullptr;
		const char* m_lastEdgeRule = nullptr;
	};
}
