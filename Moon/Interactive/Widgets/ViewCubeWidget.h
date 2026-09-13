#pragma once
#include "Interactive/Screen/ScreenWidget.h"

namespace MOON
{
	/** Rotate buttons drawn as a 2D overlay around the navigation cube.
	 *
	 * The widget does not draw the cube itself (ImRenderer::drawSort() owns the
	 * cube, its cells and its 3D hit test); it adds four arrow buttons around it
	 * and turns a click on one of them into a 90 degree turntable rotation of
	 * the camera. Everything else - the cursor space, the button shapes, the hit
	 * test, the state machine and telling the scene that the cursor is taken -
	 * comes from ScreenWidget.
	 */
	class ViewCubeWidget : public ScreenWidget
	{
	public:
		enum class EAction
		{
			None = 0,
			OrbitUp,
			OrbitRight,
			OrbitDown,
			OrbitLeft
		};

		ViewCubeWidget(const std::string& name);
		virtual ~ViewCubeWidget();

		/** Rotation applied by a single click, in degrees. */
		void SetStepDegrees(float p_degrees);
		float GetStepDegrees() const { return mStepDegrees; }

	protected:
		ScreenLayout BuildLayout() const override;
		void BuildShapes(std::vector<HitShape>& p_outShapes) const override;
		void DrawContent(ImDrawList& p_drawList, const ScreenRect& p_rect) override;
		void OnAction(int p_action) override;
		/** Hidden while the camera is locked, e.g. while sketching. */
		bool IsInteractionEnabled() const override;

	private:
		void ApplyRotation(EAction p_action);

	private:
		float mStepDegrees = 90.0f;
	};
}
