#pragma once
#include "Interactive/Screen/ScreenWidget.h"

#include <vector>

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
	 *
	 * A button is its icon: the glyph is drawn on a ring around the cube and the
	 * click target is a circle under it (see BuildShapes), so there is no outline
	 * to author and no geometry kernel involved. Editing the look of a button means
	 * editing the numbers below.
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
			OrbitLeft,
			/** Turn about the view axis: the view spins in place instead of
			 * looking from a different side. The two buttons sit at the south
			 * east and the south west of the ring. */
			RollClockwise,
			RollCounterClockwise
		};

		ViewCubeWidget(const std::string& name);
		virtual ~ViewCubeWidget();

		/** Rotation applied by a single click, in degrees. */
		void SetStepDegrees(float p_degrees);
		float GetStepDegrees() const { return mStepDegrees; }

	protected:
		ScreenLayout BuildLayout() const override;
		void BuildShapes(std::vector<HitShape>& p_outShapes) const override;
		/** Draws the six button icons, on the ring their click targets sit on. */
	void DrawIcons(::Rendering::Overlay::Im2D::DrawList& p_drawList, const ScreenRect& p_rect);

	void DrawContent(::Rendering::Overlay::Im2D::DrawList& p_drawList, const ScreenRect& p_rect) override;
		void OnAction(int p_action) override;
		/** Hidden while the camera is locked, e.g. while sketching. */
		bool IsInteractionEnabled() const override;

	private:
		void ApplyRotation(EAction p_action);

	private:
		/** Turn applied by one click. The buttons fit the view afterwards, the
		 * same way a click on a face of the cube does. */
		float mStepDegrees = 45.0f;
	};
}
