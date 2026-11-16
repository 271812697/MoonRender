#include "treesync.h"

#include "treeViewpanel.h"

#include "Core/Global/ServiceLocator.h"
#include "core/log.h"

namespace MOON
{
	TreeViewSync& TreeViewSync::Instance()
	{
		static TreeViewSync sync;
		return sync;
	}

	void TreeViewSync::Add(Core::ECS::Actor* p_actor)
	{
		if (p_actor != nullptr)
		{
			m_added.push_back(p_actor);
		}
	}

	void TreeViewSync::Remove(Core::ECS::Actor* p_actor)
	{
		if (p_actor != nullptr)
		{
			m_removed.push_back(p_actor);
		}
	}

	void TreeViewSync::Update(Core::ECS::Actor* p_actor)
	{
		if (p_actor != nullptr)
		{
			m_modified.push_back(p_actor);
		}
	}

	void TreeViewSync::Refresh()
	{
		m_refresh = true;
	}

	bool TreeViewSync::IsPending() const
	{
		return !m_added.empty() || !m_removed.empty() || !m_modified.empty() || m_refresh;
	}

	bool TreeViewSync::Flush()
	{
		if (!IsPending())
		{
			return false;
		}

		// Nothing is ever provided before the editor has built its panels, and an actor can
		// ask for a row from a job thread during start-up: the request is dropped instead of
		// being kept against a tree that may never come.
		if (!::Core::Global::ServiceLocator::IsProvided<TreeViewPanel>())
		{
			m_added.clear();
			m_removed.clear();
			m_modified.clear();
			m_refresh = false;
			return false;
		}

		const bool hasOperations = !m_added.empty() || !m_removed.empty() || !m_modified.empty();
		if (hasOperations)
		{
			std::vector<TreeViewPanel::Operation> operations;
			operations.push_back(TreeViewPanel::Operation(TreeViewPanel::OperationType::Add, m_added));
			operations.push_back(TreeViewPanel::Operation(TreeViewPanel::OperationType::Remove, m_removed));
			operations.push_back(TreeViewPanel::Operation(TreeViewPanel::OperationType::Update, m_modified));
			GetTreeView.updateActorInTree(operations);
			m_added.clear();
			m_removed.clear();
			m_modified.clear();
			// A refresh asked for in the same frame waits for the next one: the rows just
			// added are part of the scene the refresh walks, and rebuilding now would add
			// them a second time.
			return true;
		}

		m_refresh = false;
		CORE_INFO("[Viewer] refreshing the tree view");
		GetTreeView.updateTreeViewSceneRoot();
		CORE_INFO("[Viewer] tree view refreshed");
		return true;
	}
}
