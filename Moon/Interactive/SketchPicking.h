#pragma once
#include <set>
#include <Maths/FMatrix4.h>
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

		/** Projects sketch coordinates to viewport pixels.
		 *
		 * Camera::GetViewPortMatrix() already carries the projection, so under a perspective
		 * camera the homogeneous w varies from point to point across the plane and has to be
		 * divided out. Multiplying the matrix by a Vector3d does not do that - the w is dropped -
		 * which is why the hit tests drifted as soon as the sketch was seen in perspective: the
		 * distances were being measured in a space where the plane's image is not the plane.
		 */
		struct SketchProjector
		{
			SketchProjector(const Maths::FMatrix4& p_viewPortMatrix, const SketcherPlane2D& p_plane)
				: m_matrix(p_viewPortMatrix), m_plane(p_plane) {
			}
			/** @return the viewport pixel of sketch coordinates (u, v). */
			Base::Vector3d operator()(double p_u, double p_v) const
			{
				const SketcherPlane2D& plane = m_plane;
				const double x = plane.origin.x + p_u * plane.xAxis.x + p_v * plane.yAxis.x;
				const double y = plane.origin.y + p_u * plane.xAxis.y + p_v * plane.yAxis.y;
				const double z = plane.origin.z + p_u * plane.xAxis.z + p_v * plane.yAxis.z;
				const float* m = m_matrix.data;
				const double px = m[0] * x + m[1] * y + m[2] * z + m[3];
				const double py = m[4] * x + m[5] * y + m[6] * z + m[7];
				const double pw = m[12] * x + m[13] * y + m[14] * z + m[15];
				// An orthographic camera leaves w at 1, so this divide is a no-op there.
				const double w = std::abs(pw) > 1.0e-9 ? pw : 1.0;
				return Base::Vector3d{ px / w, py / w, 0.0 };
			}
			/** The same for a sketch-plane point given as a 3D vector with z ignored. */
			Base::Vector3d operator()(const Base::Vector3d& p_uv) const
			{
				return (*this)(p_uv.x, p_uv.y);
			}
			Maths::FMatrix4 m_matrix;
			SketcherPlane2D m_plane;
		};

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
			/** The adaptive grid step, in sketch units: one minor cell is about
			 * SketcherObj::gridTargetPx() pixels across, snapped up to a
			 * 1 / 2 x 10^n value. */
			float step = 0.0f;
		};

		/** Which curve of the sketch a viewport position is on, or NoGeoId. */
		int pickGeoIndex(SketcherObj& p_sketch, const Base::Vector2d& p_pos, const Maths::FMatrix4& p_viewPortMatrix);
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

		/** The adaptive grid step for a given on-screen scale: the 1 / 2 x 10^n value that puts
		 * a cell closest to p_targetPx pixels across. Shared with the perspective grid, which
		 * has no GridView to read a step from, so the two lattices stay the same lattice. */
		float gridStepForScale(float p_targetPx, float p_pixelsPerUnit);

		/** The adaptive grid step of the view, whichever projection it is using.
		 *
		 * Both the lattice the sketch draws and the one the cursor snaps to come from here, so
		 * they are the same lattice - including in a perspective view, where gridView() (which
		 * describes the plane in view space) has nothing to say and the scale is taken where the
		 * view looks instead: the plane point under the centre of the viewport.
		 *
		 * @return false when there is no lattice to speak of: no camera, a viewport that has not
		 *         been sized, or a view looking along the plane. */
		bool gridStep(SketcherObj& p_sketch, Editor::Panels::SceneView& p_view,
			ImRenderer& p_renderer, float& p_step);
	}
}
