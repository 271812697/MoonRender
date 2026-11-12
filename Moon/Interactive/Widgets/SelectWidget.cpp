#include "Interactive/Widgets/SelectWidget.h"

#include "renderer/SceneView.h"
#include "Interactive/Im3DRenderer.h"
#include "Interactive/Screen/ScreenOverlayRegistry.h"
#include "Core/Global/ServiceLocator.h"
#include "core/SelectionManager.h"
#include "editor/UI/TreeViewPanel/treeViewpanel.h"

namespace MOON
{
	SelectWidget::SelectWidget(const std::string& name) :EventWidget(name)
	{
	}

	SelectWidget::~SelectWidget()
	{
	}

	void SelectWidget::onLeftMousePressed()
	{
		const auto [x, y] = m_sceneView->getInutState().GetMousePosition();
		m_rectStartX = static_cast<float>(x);
		m_rectStartY = static_cast<float>(y);
		m_rectEndX = m_rectStartX;
		m_rectEndY = m_rectStartY;
		publishPick();
	}

	void SelectWidget::onLeftMouseReleased()
	{
		// Nothing yet: the rectangle pick (replace the selection with everything inside the
		// rectangle) is not implemented. The corners above are what it will need.
	}

	void SelectWidget::onMouseMove()
	{
		const auto [x, y] = m_sceneView->getInutState().GetMousePosition();
		m_rectEndX = static_cast<float>(x);
		m_rectEndY = static_cast<float>(y);
		updatePreselect();
	}

	void SelectWidget::onKeyPress(const std::string& key)
	{
		if (key == "CONTROL_L")
		{
			GetSelection.setSelectMode(SelectMode::AddSelect);
		}
	}

	void SelectWidget::onKeyRelease(const std::string& key)
	{
		if (key == "CONTROL_L")
		{
			GetSelection.setSelectMode(SelectMode::OverrideSelect);
		}
	}

	void SelectWidget::publishPick()
	{
		// The click has to be a click on the model, and an overlay is not the model:
		//
		//  - a 3D handle is drawn over the scene but is not part of it, so pressing one would
		//    otherwise publish the face or edge behind it - replacing the operation the handle
		//    belongs to with another one (see EventWidget::IsUsingMouse);
		//  - a 2D widget (the navigation cube, the split handles) owns its pixels outright, which
		//    is what ScreenOverlayRegistry answers for the scene picking as well.
		if (ImRenderer::instance().IsAnyWidgetUsingMouse())
		{
			return;
		}
		{
			const auto [x, y] = m_sceneView->getInutState().GetMousePosition();
			if (ScreenOverlayRegistry::Instance().BlocksSceneCursor(static_cast<float>(x),
				static_cast<float>(y)))
			{
				return;
			}
		}
		const auto pick = m_sceneView->GetPickResult();
		if (pick.has_value())
		{
			if (const auto actor = std::get_if<Tools::Utils::OptRef<::Core::ECS::Actor>>(&pick.value()))
			{
				if (*actor)
				{
					GetTreeView.highlightByActor(&actor->value());
					// This is the click in the viewport turned into a selection event: the tree
					// follows it, the task panels hear it, and a task that can be built on the
					// face can open from it (see SelectionTaskLauncher).
					SelectionManager::instance().select({ actor->value().GetID() });
					return;
				}
			}
		}
		// Nothing under the cursor: the click empties the selection, which is what clicking empty
		// space means everywhere.
		SelectionManager::instance().select({});
		GetTreeView.clearHighlight();
	}

	void SelectWidget::updatePreselect()
	{
		// Nothing behind an overlay is preselected either: hovering a handle or a 2D widget
		// highlights that, not the face it happens to cover.
		if (ImRenderer::instance().IsAnyWidgetUsingMouse())
		{
			GetSelection.clearPreselect();
			return;
		}
		{
			const auto [x, y] = m_sceneView->getInutState().GetMousePosition();
			if (ScreenOverlayRegistry::Instance().BlocksSceneCursor(static_cast<float>(x),
				static_cast<float>(y)))
			{
				GetSelection.clearPreselect();
				return;
			}
		}
		const auto pick = m_sceneView->GetPickResult();
		if (pick.has_value())
		{
			if (const auto actor = std::get_if<Tools::Utils::OptRef<::Core::ECS::Actor>>(&pick.value()))
			{
				if (*actor)
				{
					GetSelection.setPreselect({ actor->value().GetID() });
					return;
				}
			}
		}
		GetSelection.clearPreselect();
	}
}
