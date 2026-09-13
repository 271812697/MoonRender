#pragma once
#include "Interactive/Screen/ScreenWidget.h"
#include <memory>
#include <vector>

namespace MOON
{
	struct ScreenPath;

	/** Reference widget for custom (path) shapes.
	 *
	 * It demonstrates the whole path pipeline in one place: the shape is baked
	 * from the currently active sketch when there is one, and falls back to a
	 * built-in star and circle otherwise.
	 *
	 * Each closed wire of the sketch becomes one shape with its own action, so
	 * clicking reports which wire was hit. The shapes are the only clickable part
	 * (the widget rectangle does not block the scene) and each is hit tested with
	 * the same geometry it draws.
	 */
	class PathShapeWidget : public ScreenWidget
	{
	public:
		PathShapeWidget(const std::string& name);
		virtual ~PathShapeWidget();

	protected:
		ScreenLayout BuildLayout() const override;
		void BuildShapes(std::vector<HitShape>& p_outShapes) const override;
		void DrawContent(ImDrawList& p_drawList, const ScreenRect& p_rect) override;
		void OnAction(int p_action) override;

	private:
		/** Re-bakes the shape when the active sketch (or its curve count)
		 * changed. Called from BuildShapes(), which is const, hence the mutable
		 * cache below. */
		void RefreshShape() const;
		/** Used when no sketch is active: two separate wires, which shows that
		 * each wire becomes its own shape. */
		void BuildFallbackWires(std::vector<ScreenPath>& p_wires) const;

	private:
		/** One entry per wire of the bake; the widget builds one shape from each. */
		mutable std::vector<std::shared_ptr<const ScreenPath>> mWirePaths;
		mutable const void* mSourceSketch = nullptr;
		mutable int mSourceCurveCount = -1;
		mutable bool mSourceIsSketch = false;
		/** Set when RefreshShape() actually rebuilt the wires, so the per frame
		 * caller can mark the hit geometry dirty. */
		mutable bool mShapeChanged = true;
	};
}
