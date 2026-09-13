#pragma once
#include "Interactive/Screen/ScreenWidget.h"

#include <memory>
#include <vector>

namespace MOON
{
	struct ScreenPath;

	/** Rotate buttons drawn as a 2D overlay around the navigation cube.
	 *
	 * The widget does not draw the cube itself (ImRenderer::drawSort() owns the
	 * cube, its cells and its 3D hit test); it adds four arrow buttons around it
	 * and turns a click on one of them into a 90 degree turntable rotation of
	 * the camera. Everything else - the cursor space, the button shapes, the hit
	 * test, the state machine and telling the scene that the cursor is taken -
	 * comes from ScreenWidget.
	 *
	 * The buttons are curves, not polygons: each one is an arc slot - a band of
	 * constant width around the cube with a rounded cap at each end - authored
	 * with ShapeBuilder (OCCT arcs), connected into a wire and turned into a
	 * face, exactly like a finished sketch would be. Editing the look of a
	 * button means editing the numbers below, no asset file involved.
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
		void DrawContent(ImDrawList& p_drawList, const ScreenRect& p_rect) override;
		void OnAction(int p_action) override;
		/** Hidden while the camera is locked, e.g. while sketching. */
		bool IsInteractionEnabled() const override;

	private:
		/** One rotate button: the outline that is drawn and hit tested, plus the
		 * action a click on it triggers. */
		struct ArrowShape
		{
			EAction action = EAction::None;
			std::shared_ptr<const ScreenPath> path;
		};

		/** Builds the arc slot of every button from OCCT curves, once. */
		void BuildArrowShapes();
		/** Used when the curves could not be built: plain arrow head triangles. */
		void BuildFallbackShapes(std::vector<HitShape>& p_outShapes) const;

		void ApplyRotation(EAction p_action);

	private:
		/** Outline of the four buttons; built once, the shapes never change. */
		std::vector<ArrowShape> mArrows;
		/** Turn applied by one click. The buttons fit the view afterwards, the
		 * same way a click on a face of the cube does. */
		float mStepDegrees = 45.0f;
	};
}
