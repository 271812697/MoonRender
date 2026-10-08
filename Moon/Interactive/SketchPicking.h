#pragma once
#include <set>
#include "Sketcher/SketcherObj.h"

namespace Editor::Panels { class SceneView; }

namespace MOON {
	class ImRenderer;

	/** Picking and snapping of a sketch's curves.
	 *
	 * These are pure geometry queries over the sketch's data that need the camera: they
	 * change nothing, and the selection they talk about lives in SketcherObj. The
	 * editing widget and the drawing tools both ask them - that is why they are free
	 * functions here instead of methods of either one: the tools have no widget to ask,
	 * and the widget is not a thing the data layer knows.
	 */
	namespace SketchPicking {
		// The vocabulary the sketch and the widgets share (see SketcherTypes.h).
		using SelectGeoId = MOON::SelectGeoId;
		using PointPos = Sketcher::PointPos;
		using CurveSegment = SketcherObj::CurveSegment;
		using SegPoint = SketcherObj::SegPoint;

		/** How the sketch plane lies in the viewport right now: what the adaptive
		 * background grid is drawn from, and what the grid snapping has to agree with -
		 * they share this so the lines the user sees and the places the cursor snaps to
		 * are one lattice. An orthographic camera projects along its own view direction,
		 * so the plane is not necessarily face on, and every length here is measured
		 * through the camera rather than assumed. */
		struct GridView
		{
			/** View-space position of the plane origin. */
			float oX = 0.0f;
			float oY = 0.0f;
			/** View-space direction the plane axes run in, i.e. how far one sketch unit
			 * along u / v travels across the screen (1 = not foreshortened). */
			float uX = 0.0f;
			float uY = 0.0f;
			float vX = 0.0f;
			float vY = 0.0f;
			/** Viewport half extents, in view-space units. */
			float hx = 0.0f;
			float hy = 0.0f;
			/** The adaptive grid step, in sketch units. */
			float step = 0.0f;
		};

		/** Which curve of the sketch a viewport position is on, or NoGeoId. */
		int pickGeoIndex(SketcherObj& p_sketch, const Base::Vector2d& p_pos, const Base::Matrix4D& p_viewPortMat);
		/** Which element of the sketch - a curve, or a marker on one - a position
		 * picks. */
		SelectGeoId testSelect(SketcherObj& p_sketch, Editor::Panels::SceneView& p_view, const Base::Vector2d& p_pos);
		/** Snaps a sketch-plane position onto a marker of a curve, or onto the grid,
		 * within the pixel tolerance the tools share. @return true when it moved. */
		bool snapPoint(SketcherObj& p_sketch, Editor::Panels::SceneView& p_view, ImRenderer& p_renderer, Base::Vector2d& p_pos, const std::set<int>& p_avoid = {});
		/** Snaps a sketch-plane position onto the grid the user sees. */
		bool snapToGridPoint(SketcherObj& p_sketch, Editor::Panels::SceneView& p_view, ImRenderer& p_renderer, Base::Vector2d& p_pos);
		/** Fills in p_out for the current camera.
		 * @return false when there is nothing to describe: no camera, a perspective one,
		 *         or an orthographic one seen along the plane (the plane is a line on
		 *         screen then, and its visible part runs off to infinity). */
		bool gridView(SketcherObj& p_sketch, Editor::Panels::SceneView& p_view, GridView& p_out);
	}
}
