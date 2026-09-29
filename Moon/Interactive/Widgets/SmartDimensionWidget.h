#pragma once
#include "Interactive/Widgets/DrawSketchHandler.h"

namespace MOON
{
	/** The smart dimension tool of the constraint toolbar.
	 *
	 * Pick one or two elements of the sketch, move the mouse to say which dimension
	 * is meant and where its line should sit, then place it: a dialog asks for the
	 * value and the constraint is added.
	 *
	 * One element:
	 *   - a line gives its length, or one of its coordinates, decided by where the
	 *     cursor sits relative to the two end points (see below);
	 *   - a point is measured against the origin the same way;
	 *   - a full circle gives its diameter and an arc of a circle its radius, with
	 *     the other one a press of M away.
	 * Two elements:
	 *   - two points, a point and a line, or two circles: the distance between them
	 *     (from the point to the line, and between the centres of the circles);
	 *   - two lines: the angle between them.
	 *
	 * The rule for a distance is FreeCAD's: the two points span a box, and where the
	 * cursor sits relative to that box decides between a horizontal distance (beside
	 * it), a vertical one (above or below it) and an aligned length (diagonally or
	 * inside). A pair of points that already runs along an axis keeps that dimension
	 * whatever the cursor does.
	 *
	 * Clicking the geometry picks it (a second click on the same element drops it
	 * again), clicking empty space places the dimension, and so does dragging out of
	 * the pick. The dialog opens when the dimension is placed, so a cancelled one
	 * leaves nothing behind. A right click ends a pick, another one leaves the tool.
	 *
	 * It is a DrawSketchHandler, which is an EventWidget: the toolbar turns it on and
	 * off, and the sketch stops handling the mouse by itself while it runs (see
	 * SketcherObj::onUpdate), so the picks belong to this tool alone.
	 */
	class SmartDimensionWidget : public DrawSketchHandler
	{
	public:
		SmartDimensionWidget(const std::string& name);
		virtual ~SmartDimensionWidget() override;

		virtual void onUpdate() override;
		virtual void onSetActive(bool flag) override;
		virtual void onLeftMousePressed() override;
		virtual void onLeftMouseReleased() override;
		virtual void onRightMousePressed() override;
		virtual void onMouseMove() override;
		virtual void onKeyPress(const std::string& key) override;

	private:
		/** Adds the element under the cursor to the picks, or drops it when it is
		 * already picked.
		 * \return false when the cursor is not on any geometry. */
		bool pickAtCursor();
		/** Where the cursor is on the sketch plane, in sketch coordinates. */
		Base::Vector2d cursorOnSketchPlane() const;
		/** Works out again which dimension the picks can take, from where the cursor
		 * is now. */
		void updateCandidate();
		/** Asks for the value and adds the constraint. */
		void applyCandidate();
		/** Draws the markup of the dimension that would be added. */
		void drawPreview();

		class Internal;
		Internal* mInternal = nullptr;
	};
}
