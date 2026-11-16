#pragma once
#include <vector>

namespace Core::ECS {
	class Actor;
}

namespace MOON
{
	/** The queue between whatever changes the scene and the tree view that shows it.
	 *
	 * Actors are created, deleted and renamed from places that must not touch a widget:
	 * the scene parser, the document reader, the features and the topology actors - some
	 * of them while a job thread is building geometry. Adding a row to a QStandardItemModel
	 * from there is not allowed, and doing it one row at a time is what made the tree
	 * flicker and rebuild itself several times per document.
	 *
	 * So they leave what they did here, and the viewport applies it once per frame, after
	 * the scene has been updated and before the frame that draws it. A flush empties the
	 * queue; a request that arrives while one is running is applied by the next one.
	 *
	 * This is deliberately not a QObject: it is reached from an actor's destructor, from a
	 * job thread and from the document reader, and none of those may depend on a widget
	 * being alive. A request made before the tree exists is dropped, not queued forever.
	 */
	class TreeViewSync final
	{
	public:
		static TreeViewSync& Instance();

		TreeViewSync(const TreeViewSync&) = delete;
		TreeViewSync& operator=(const TreeViewSync&) = delete;

		/** Adds p_actor (with the children it already has) under the scene root. */
		void Add(Core::ECS::Actor* p_actor);
		/** Takes the row of p_actor out of the tree. The actor itself is not touched. */
		void Remove(Core::ECS::Actor* p_actor);
		/** Re-reads the row of p_actor: its name, its active state, its children. */
		void Update(Core::ECS::Actor* p_actor);
		/** Rebuilds the whole tree from the scene - what a document read needs, because
		 * discretizing a shape rebuilds the topology actors under it. */
		void Refresh();

		/** Whether anything is waiting to be applied. */
		bool IsPending() const;

		/** Applies what is queued, in the order add / remove / update, and empties the
		 * queues. Has to run on the thread that owns the tree widget: the editor's frame
		 * loop. A refresh that is asked for while operations are pending is left for the
		 * next flush, so a row that is about to be added is not added twice.
		 * @return true when something was applied. */
		bool Flush();

	private:
		TreeViewSync() = default;

		std::vector<Core::ECS::Actor*> m_added;
		std::vector<Core::ECS::Actor*> m_removed;
		std::vector<Core::ECS::Actor*> m_modified;
		bool m_refresh = false;
	};
}

/** The queue every scene change that shows in the tree goes through, wherever it happens -
 * an actor's constructor or destructor, a feature rebuild, a document read. Fully qualified
 * because it is used from namespaces that are not MOON's. */
#define GetTreeViewSync ::MOON::TreeViewSync::Instance()
