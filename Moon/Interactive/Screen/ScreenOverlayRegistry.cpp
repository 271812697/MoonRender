#include "Interactive/Screen/ScreenOverlayRegistry.h"
#include "Interactive/Screen/ScreenWidget.h"
#include <algorithm>

namespace MOON
{
	ScreenOverlayRegistry& ScreenOverlayRegistry::Instance()
	{
		static ScreenOverlayRegistry registry;
		return registry;
	}

	void ScreenOverlayRegistry::Register(ScreenWidget* p_widget)
	{
		if (p_widget == nullptr)
		{
			return;
		}
		if (std::find(mWidgets.begin(), mWidgets.end(), p_widget) == mWidgets.end())
		{
			mWidgets.push_back(p_widget);
		}
	}

	void ScreenOverlayRegistry::Unregister(ScreenWidget* p_widget)
	{
		const auto it = std::find(mWidgets.begin(), mWidgets.end(), p_widget);
		if (it != mWidgets.end())
		{
			mWidgets.erase(it);
		}
	}

	ScreenWidget* ScreenOverlayRegistry::GetOwnerAt(float p_x, float p_y) const
	{
		// The draw order of the widgets is not defined (they live in an
		// unordered_map in the renderer), so overlapping widgets are resolved by
		// an explicit z order instead of by iteration order.
		ScreenWidget* owner = nullptr;
		float bestZOrder = 0.0f;
		for (ScreenWidget* widget : mWidgets)
		{
			if (widget == nullptr || !widget->BlocksSceneCursor(p_x, p_y))
			{
				continue;
			}
			if (owner == nullptr || widget->GetZOrder() > bestZOrder)
			{
				owner = widget;
				bestZOrder = widget->GetZOrder();
			}
		}
		return owner;
	}

	bool ScreenOverlayRegistry::BlocksSceneCursor(float p_x, float p_y) const
	{
		return GetOwnerAt(p_x, p_y) != nullptr;
	}

	bool ScreenOverlayRegistry::HitsShape(float p_x, float p_y) const
	{
		for (ScreenWidget* widget : mWidgets)
		{
			if (widget != nullptr && widget->HitsShape(p_x, p_y))
			{
				return true;
			}
		}
		return false;
	}

	bool ScreenOverlayRegistry::IsCapturing() const
	{
		for (ScreenWidget* widget : mWidgets)
		{
			if (widget != nullptr && widget->IsCapturing())
			{
				return true;
			}
		}
		return false;
	}
}
