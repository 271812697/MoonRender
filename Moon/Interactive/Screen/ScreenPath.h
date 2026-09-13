#pragma once
#include "Interactive/Screen/ScreenLayout.h"
#include "Qtimgui/imgui/imgui.h"
#include <vector>

namespace MOON
{
	/** A flattened 2D outline in widget local space.
	 *
	 * Built either by hand (a list of points) or by flattening sketch curves
	 * (see SketchPathBake). Multiple loops express holes: hit testing uses the
	 * even-odd rule across all loops, so a loop nested inside another one punches
	 * a hole in it.
	 *
	 * Points are widget local pixels, the same space HitShape uses, so a path
	 * shape can be hit tested and drawn with the single description the rest of
	 * the shape layer relies on.
	 */
	struct ScreenPath
	{
		/** Sub paths. Open ones (a polyline) are legal: they are drawn stroked
		 * and, when used for filling, treated as if closed. */
		std::vector<std::vector<ImVec2>> loops;
		std::vector<bool> closed;

		ImVec2 boundsMin{ 0.0f, 0.0f };
		ImVec2 boundsMax{ 0.0f, 0.0f };
		bool boundsValid = false;

		bool IsEmpty() const { return loops.empty(); }

		void RecomputeBounds();
		ImVec2 Size() const;
		ImVec2 Center() const;

		/** Scales / rotates / translates every point in place.
		 * p_flipY mirrors around the shape center, which is what turns sketch
		 * space (y up) into screen space (y down). */
		void Transform(ImVec2 p_scale, float p_rotationDeg, ImVec2 p_translation, bool p_flipY);

		/** Fits the shape into p_rect preserving the aspect ratio, optionally
		 * flipping y. Used to map a baked sketch into a widget rectangle. */
		void FitInto(const ScreenRect& p_rect, bool p_flipY);

		/** Even-odd test across every loop. */
		bool ContainsPoint(const ImVec2& p_local) const;
		/** Fill plus a growing margin: inside the shape or within p_pad of the
		 * outline. This is how HitShape::pad behaves for filled paths, where the
		 * outline cannot simply be scaled outward. */
		bool ContainsPointWithPad(const ImVec2& p_local, float p_pad) const;
		/** Distance from p_local to the nearest outline segment. */
		float DistanceToOutline(const ImVec2& p_local) const;
		/** Stroke test: within p_halfWidth of the outline. */
		bool NearOutline(const ImVec2& p_local, float p_halfWidth) const;

		/** Triangle list (3 vertices per triangle) for filling loop 0.
		 *
		 * ImGui's own concave fill could not be trusted here: on a shape whose
		 * winding it does not expect it falls back to emitting triangles that
		 * cover the convex hull, which made a plain star fill outside itself.
		 * The triangulation is therefore done here, is winding agnostic, and is
		 * validated before use - an outline that cannot be triangulated returns
		 * an empty list (nothing is filled) and logs a warning instead of
		 * drawing garbage.
		 *
		 * The fill region follows the even-odd rule over the closed loops, so a
		 * loop nested inside another one becomes a hole (the rule the hit test
		 * already uses). Open loops are never filled.
		 *
		 * The result is cached and invalidated by RecomputeBounds(), so callers
		 * must go through it after changing the loops.
		 */
		const std::vector<ImVec2>& GetFillTriangles() const;
		/** Nesting depth of every closed loop (0 = outermost) plus the smallest
		 * loop containing it, used by the even-odd fill. */
		void ComputeNesting(std::vector<int>& p_outDepths, std::vector<int>& p_outParents) const;
		/** Even-odd triangulation over all closed loops (holes included). */
		bool TriangulateEvenOdd(std::vector<ImVec2>& p_outTriangles) const;
		/** Ear clipping of one simple polygon; appends to p_outTriangles. */
		bool TriangulateSimplePolygon(
			const std::vector<ImVec2>& p_points,
			std::vector<ImVec2>& p_outTriangles) const;
		/** True when the triangles cover the even-odd region exactly and every
		 * one of them lies inside it. */
		bool ValidateTriangulation(const std::vector<ImVec2>& p_triangles) const;

		/** Cache for GetFillTriangles(); reset whenever the geometry changes. */
		mutable std::vector<ImVec2> mFillTriangles;
		mutable bool mFillTrianglesValid = false;

		/** Writes the loops next to the executable so a rejected fill can be
		 * analysed offline (Build/bin/Release/screenpath_dump.txt). */
		void DumpForDebug() const;
	};
}
