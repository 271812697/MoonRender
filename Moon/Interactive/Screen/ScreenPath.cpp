#include "Interactive/Screen/ScreenPath.h"
#include "core/log.h"
#include <Tools/Utils/PathParser.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <fstream>

namespace MOON
{
	namespace
	{
		/** Squared distance from p to the segment a-b. */
		float DistanceToSegmentSquared(const ScreenPoint& p, const ScreenPoint& a, const ScreenPoint& b)
		{
			const float abX = b.x - a.x;
			const float abY = b.y - a.y;
			const float lengthSquared = abX * abX + abY * abY;
			if (lengthSquared <= 1e-12f)
			{
				const float dx = p.x - a.x;
				const float dy = p.y - a.y;
				return dx * dx + dy * dy;
			}
			float t = ((p.x - a.x) * abX + (p.y - a.y) * abY) / lengthSquared;
			t = std::clamp(t, 0.0f, 1.0f);
			const float dx = p.x - (a.x + abX * t);
			const float dy = p.y - (a.y + abY * t);
			return dx * dx + dy * dy;
		}

		/** Cross product of (b - a) and (c - a); > 0 means a convex corner for a
		 * counter-clockwise polygon. */
		float Cross3(const ScreenPoint& a, const ScreenPoint& b, const ScreenPoint& c)
		{
			return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
		}

		/** Strictly inside a counter-clockwise triangle; points on the edge count
		 * as outside, which keeps ear clipping conservative. */
		bool PointInTriangleStrict(
			const ScreenPoint& p,
			const ScreenPoint& a,
			const ScreenPoint& b,
			const ScreenPoint& c)
		{
			constexpr float kEpsilon = 1e-6f;
			return Cross3(a, b, p) > kEpsilon
				&& Cross3(b, c, p) > kEpsilon
				&& Cross3(c, a, p) > kEpsilon;
		}

		/** Inside the triangle with at least p_margin distance from every edge.
		 *
		 * Used as the second ear-clipping pass: tangent geometry (two arcs
		 * meeting tangentially, as in a slot) can leave a vertex glued to an ear's
		 * edge, which blocks every candidate in the strict pass.
		 */
		bool PointInTriangleByMargin(
			const ScreenPoint& p,
			const ScreenPoint& a,
			const ScreenPoint& b,
			const ScreenPoint& c,
			float p_margin)
		{
			const float sideAB = Cross3(a, b, p);
			const float sideBC = Cross3(b, c, p);
			const float sideCA = Cross3(c, a, p);
			if (sideAB <= 0.0f || sideBC <= 0.0f || sideCA <= 0.0f)
			{
				return false;
			}
			// cross / edge length is the distance to that edge.
			const float lengthAB = std::max(std::sqrt(
				(b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y)), 1e-6f);
			const float lengthBC = std::max(std::sqrt(
				(c.x - b.x) * (c.x - b.x) + (c.y - b.y) * (c.y - b.y)), 1e-6f);
			const float lengthCA = std::max(std::sqrt(
				(a.x - c.x) * (a.x - c.x) + (a.y - c.y) * (a.y - c.y)), 1e-6f);
			return sideAB / lengthAB > p_margin
				&& sideBC / lengthBC > p_margin
				&& sideCA / lengthCA > p_margin;
		}

		/** Twice the signed area of a closed outline. */
		float SignedDoubleArea(const std::vector<ScreenPoint>& p_loop)
		{
			float sum = 0.0f;
			for (size_t i = 0; i < p_loop.size(); ++i)
			{
				const ScreenPoint& a = p_loop[i];
				const ScreenPoint& b = p_loop[(i + 1) % p_loop.size()];
				sum += a.x * b.y - b.x * a.y;
			}
			return sum;
		}

		/** True when p_current lies on the straight line between its neighbours,
		 * so it is not a corner and carries no area. A point that doubles back
		 * (a spike) is kept. */
		bool IsStraightContinuation(
			const ScreenPoint& p_previous,
			const ScreenPoint& p_current,
			const ScreenPoint& p_next)
		{
			const float incomingX = p_current.x - p_previous.x;
			const float incomingY = p_current.y - p_previous.y;
			const float outgoingX = p_next.x - p_current.x;
			const float outgoingY = p_next.y - p_current.y;
			const float incomingLength = std::sqrt(incomingX * incomingX + incomingY * incomingY);
			const float outgoingLength = std::sqrt(outgoingX * outgoingX + outgoingY * outgoingY);
			if (incomingLength <= 1e-6f || outgoingLength <= 1e-6f)
			{
				return true;   // duplicate point
			}
			const float cross = Cross3(p_previous, p_current, p_next);
			const float scale = std::max(incomingLength, outgoingLength);
			const float dotValue = incomingX * outgoingX + incomingY * outgoingY;
			return std::abs(cross) <= 1e-6f * scale * scale && dotValue > 0.0f;
		}

		/** Removes duplicate points and points on a straight line. A straight
		 * chain stalls the ear clipping: no corner on a line is convex, so
		 * nothing can be clipped any more. */
		std::vector<ScreenPoint> CleanOutline(const std::vector<ScreenPoint>& p_loop)
		{
			std::vector<ScreenPoint> cleaned;
			cleaned.reserve(p_loop.size());
			const size_t count = p_loop.size();
			for (size_t i = 0; i < count; ++i)
			{
				if (IsStraightContinuation(
					p_loop[(i + count - 1) % count],
					p_loop[i],
					p_loop[(i + 1) % count]))
				{
					continue;
				}
				cleaned.push_back(p_loop[i]);
			}
			return cleaned;
		}

		/** Crossing test for one loop (the even-odd building block). */
		bool PointInLoop(const std::vector<ScreenPoint>& p_loop, const ScreenPoint& p_point)
		{
			bool inside = false;
			const size_t count = p_loop.size();
			if (count < 3)
			{
				return false;
			}
			for (size_t i = 0, j = count - 1; i < count; j = i++)
			{
				const ScreenPoint& a = p_loop[i];
				const ScreenPoint& b = p_loop[j];
				if ((a.y > p_point.y) == (b.y > p_point.y))
				{
					continue;
				}
				const float denominator = b.y - a.y;
				if (std::abs(denominator) <= 1e-12f)
				{
					continue;
				}
				const float crossingX =
					a.x + (p_point.y - a.y) / denominator * (b.x - a.x);
				if (p_point.x < crossingX)
				{
					inside = !inside;
				}
			}
			return inside;
		}

		/** Proper crossing of two segments (they may not merely touch). */
		bool SegmentsCrossProperly(
			const ScreenPoint& p1,
			const ScreenPoint& p2,
			const ScreenPoint& p3,
			const ScreenPoint& p4)
		{
			const float d1 = Cross3(p3, p4, p1);
			const float d2 = Cross3(p3, p4, p2);
			const float d3 = Cross3(p1, p2, p3);
			const float d4 = Cross3(p1, p2, p4);
			return ((d1 > 0.0f) != (d2 > 0.0f))
				&& ((d3 > 0.0f) != (d4 > 0.0f));
		}

		/** Counts self intersections of a closed loop and remembers the first few.
		 *
		 * A loop that crosses itself makes the even-odd test and the triangulated
		 * area disagree, which is exactly what "area matches but centroids are
		 * outside" looks like from the outside.
		 */
		int CountSelfIntersections(
			const std::vector<ScreenPoint>& p_loop,
			std::vector<ScreenPoint>& p_outFirstCrossings,
			int p_maxCrossings = 4)
		{
			const size_t count = p_loop.size();
			if (count < 4)
			{
				return 0;
			}
			int crossings = 0;
			for (size_t i = 0; i < count; ++i)
			{
				const size_t iNext = (i + 1) % count;
				for (size_t j = i + 1; j < count; ++j)
				{
					const size_t jNext = (j + 1) % count;
					// Skip adjacent segments (they share an endpoint by design).
					if (i == j || iNext == j || jNext == i)
					{
						continue;
					}
					if (SegmentsCrossProperly(
						p_loop[i], p_loop[iNext], p_loop[j], p_loop[jNext]))
					{
						++crossings;
						if (static_cast<int>(p_outFirstCrossings.size() / 2) < p_maxCrossings)
						{
							p_outFirstCrossings.push_back(p_loop[i]);
							p_outFirstCrossings.push_back(p_loop[j]);
						}
					}
				}
			}
			return crossings;
		}

		/** Rightmost vertex: the representative point used to find out which
		 * loops contain another one. */
		size_t RightmostIndex(const std::vector<ScreenPoint>& p_loop)
		{
			size_t best = 0;
			for (size_t i = 1; i < p_loop.size(); ++i)
			{
				const ScreenPoint& point = p_loop[i];
				const ScreenPoint& current = p_loop[best];
				if (point.x > current.x || (point.x == current.x && point.y > current.y))
				{
					best = i;
				}
			}
			return best;
		}

		ScreenPoint RightmostPoint(const std::vector<ScreenPoint>& p_loop)
		{
			return p_loop[RightmostIndex(p_loop)];
		}

		std::vector<ScreenPoint> AsWinding(const std::vector<ScreenPoint>& p_loop, bool p_counterClockwise)
		{
			const bool isCounterClockwise = SignedDoubleArea(p_loop) > 0.0f;
			if (isCounterClockwise == p_counterClockwise)
			{
				return p_loop;
			}
			std::vector<ScreenPoint> reversed(p_loop.rbegin(), p_loop.rend());
			return reversed;
		}

		/** Splices a hole into the polygon with a bridge, producing a single
		 * "keyhole" polygon that the ear clipping can handle. */
		bool BridgeHole(std::vector<ScreenPoint>& p_polygon, const std::vector<ScreenPoint>& p_hole)
		{
			if (p_hole.size() < 3 || p_polygon.size() < 3)
			{
				return false;
			}

			const size_t holeStart = RightmostIndex(p_hole);
			const ScreenPoint bridgeStart = p_hole[holeStart];

			// Closest polygon edge crossing the ray bridgeStart -> +x.
			float bestX = 0.0f;
			size_t bestEdge = p_polygon.size();
			for (size_t i = 0; i < p_polygon.size(); ++i)
			{
				const ScreenPoint& a = p_polygon[i];
				const ScreenPoint& b = p_polygon[(i + 1) % p_polygon.size()];
				if ((a.y > bridgeStart.y) == (b.y > bridgeStart.y))
				{
					continue;
				}
				const float denominator = b.y - a.y;
				if (std::abs(denominator) <= 1e-12f)
				{
					continue;
				}
				const float x = a.x + (bridgeStart.y - a.y) / denominator * (b.x - a.x);
				if (x > bridgeStart.x && (bestEdge == p_polygon.size() || x < bestX))
				{
					bestX = x;
					bestEdge = i;
				}
			}
			if (bestEdge == p_polygon.size())
			{
				return false;
			}

			// Bridge to the end of that edge which reaches furthest to the right.
			const size_t nextEdge = (bestEdge + 1) % p_polygon.size();
			const size_t candidate = p_polygon[bestEdge].x > p_polygon[nextEdge].x
				? bestEdge
				: nextEdge;

			std::vector<ScreenPoint> merged;
			merged.reserve(p_polygon.size() + p_hole.size() + 2);
			merged.insert(merged.end(), p_polygon.begin(), p_polygon.begin() + candidate + 1);
			// Walk the hole starting at its rightmost vertex, then close it.
			for (size_t i = 0; i < p_hole.size(); ++i)
			{
				merged.push_back(p_hole[(holeStart + i) % p_hole.size()]);
			}
			merged.push_back(bridgeStart);
			merged.insert(merged.end(), p_polygon.begin() + candidate, p_polygon.end());
			p_polygon = std::move(merged);
			return true;
		}
	}

	void ScreenPath::RecomputeBounds()
	{
		boundsValid = false;
		// Any geometry change invalidates the cached fill triangulation.
		mFillTrianglesValid = false;
		mFillTriangles.clear();
		for (const std::vector<ScreenPoint>& loop : loops)
		{
			for (const ScreenPoint& point : loop)
			{
				if (!boundsValid)
				{
					boundsMin = point;
					boundsMax = point;
					boundsValid = true;
				}
				else
				{
					boundsMin.x = std::min(boundsMin.x, point.x);
					boundsMin.y = std::min(boundsMin.y, point.y);
					boundsMax.x = std::max(boundsMax.x, point.x);
					boundsMax.y = std::max(boundsMax.y, point.y);
				}
			}
		}
	}

	ScreenPoint ScreenPath::Size() const
	{
		if (!boundsValid)
		{
			return ScreenPoint(0.0f, 0.0f);
		}
		return ScreenPoint(boundsMax.x - boundsMin.x, boundsMax.y - boundsMin.y);
	}

	ScreenPoint ScreenPath::Center() const
	{
		if (!boundsValid)
		{
			return ScreenPoint(0.0f, 0.0f);
		}
		return ScreenPoint(
			(boundsMin.x + boundsMax.x) * 0.5f,
			(boundsMin.y + boundsMax.y) * 0.5f);
	}

	void ScreenPath::Transform(
		ScreenPoint p_scale,
		float p_rotationDeg,
		ScreenPoint p_translation,
		bool p_flipY)
	{
		const float radians = p_rotationDeg * 3.14159265358979f / 180.0f;
		const float cosValue = std::cos(radians);
		const float sinValue = std::sin(radians);
		const ScreenPoint pivot = Center();
		const float flip = p_flipY ? -1.0f : 1.0f;

		for (std::vector<ScreenPoint>& loop : loops)
		{
			for (ScreenPoint& point : loop)
			{
				const float localX = (point.x - pivot.x) * p_scale.x;
				const float localY = (point.y - pivot.y) * p_scale.y * flip;
				const float rotatedX = localX * cosValue - localY * sinValue;
				const float rotatedY = localX * sinValue + localY * cosValue;
				point.x = pivot.x + rotatedX + p_translation.x;
				point.y = pivot.y + rotatedY + p_translation.y;
			}
		}
		RecomputeBounds();
	}

	void ScreenPath::FitInto(const ScreenRect& p_rect, bool p_flipY)
	{
		RecomputeBounds();
		const ScreenPoint size = Size();
		if (!boundsValid || size.x <= 1e-6f || size.y <= 1e-6f)
		{
			return;
		}

		const float scale = std::min(p_rect.w / size.x, p_rect.h / size.y);
		const ScreenPoint shapeCenter = Center();
		const ScreenPoint targetCenter(p_rect.x + p_rect.w * 0.5f, p_rect.y + p_rect.h * 0.5f);
		const float flip = p_flipY ? -1.0f : 1.0f;

		for (std::vector<ScreenPoint>& loop : loops)
		{
			for (ScreenPoint& point : loop)
			{
				point.x = targetCenter.x + (point.x - shapeCenter.x) * scale;
				point.y = targetCenter.y + (point.y - shapeCenter.y) * scale * flip;
			}
		}
		RecomputeBounds();
	}

	float ScreenPath::DistanceToOutline(const ScreenPoint& p_local) const
	{
		float bestSquared = FLT_MAX;
		for (size_t loopIndex = 0; loopIndex < loops.size(); ++loopIndex)
		{
			const std::vector<ScreenPoint>& loop = loops[loopIndex];
			if (loop.size() < 2)
			{
				continue;
			}
			const bool isClosed = loopIndex < closed.size() ? closed[loopIndex] : false;
			for (size_t i = 0; i + 1 < loop.size(); ++i)
			{
				bestSquared = std::min(
					bestSquared,
					DistanceToSegmentSquared(p_local, loop[i], loop[i + 1]));
			}
			if (isClosed && loop.size() > 2)
			{
				bestSquared = std::min(
					bestSquared,
					DistanceToSegmentSquared(p_local, loop.back(), loop.front()));
			}
		}
		return bestSquared == FLT_MAX ? FLT_MAX : std::sqrt(bestSquared);
	}

	bool ScreenPath::NearOutline(const ScreenPoint& p_local, float p_halfWidth) const
	{
		return DistanceToOutline(p_local) <= p_halfWidth;
	}

	bool ScreenPath::ContainsPoint(const ScreenPoint& p_local) const
	{
		// Even-odd across the closed loops, so a loop nested inside another one
		// punches a hole. Open loops are strokes, they have no interior.
		bool inside = false;
		for (size_t i = 0; i < loops.size(); ++i)
		{
			if (i < closed.size() && !closed[i])
			{
				continue;
			}
			if (PointInLoop(loops[i], p_local))
			{
				inside = !inside;
			}
		}
		return inside;
	}

	bool ScreenPath::ContainsPointWithPad(const ScreenPoint& p_local, float p_pad) const
	{
		if (ContainsPoint(p_local))
		{
			return true;
		}
		// An arbitrary outline cannot be grown by scaling it, so the margin is
		// expressed as "close enough to the outline".
		return p_pad > 0.0f && NearOutline(p_local, p_pad);
	}

	void ScreenPath::ComputeNesting(
		std::vector<int>& p_outDepths,
		std::vector<int>& p_outParents) const
	{
		p_outDepths.assign(loops.size(), 0);
		p_outParents.assign(loops.size(), -1);

		std::vector<std::vector<ScreenPoint>> cleaned;
		std::vector<size_t> sourceIndex;
		for (size_t i = 0; i < loops.size(); ++i)
		{
			if (i < closed.size() && !closed[i])
			{
				continue;
			}
			std::vector<ScreenPoint> loop = CleanOutline(loops[i]);
			if (loop.size() < 3)
			{
				continue;
			}
			cleaned.push_back(std::move(loop));
			sourceIndex.push_back(i);
		}

		for (size_t i = 0; i < cleaned.size(); ++i)
		{
			const ScreenPoint representative = RightmostPoint(cleaned[i]);
			int depth = 0;
			int parent = -1;
			float parentArea = FLT_MAX;
			for (size_t j = 0; j < cleaned.size(); ++j)
			{
				if (i == j || !PointInLoop(cleaned[j], representative))
				{
					continue;
				}
				++depth;
				const float area = std::abs(SignedDoubleArea(cleaned[j]));
				if (area < parentArea)
				{
					parentArea = area;
					parent = static_cast<int>(j);
				}
			}
			p_outDepths[sourceIndex[i]] = depth;
			p_outParents[sourceIndex[i]] = parent >= 0
				? static_cast<int>(sourceIndex[static_cast<size_t>(parent)])
				: -1;
		}
	}

	bool ScreenPath::TriangulateSimplePolygon(
		const std::vector<ScreenPoint>& p_points,
		std::vector<ScreenPoint>& p_outTriangles) const
	{
		const std::vector<ScreenPoint> loop = CleanOutline(p_points);
		const size_t pointCount = loop.size();
		if (pointCount < 3)
		{
			return false;
		}

		// Degenerate triangles (zero area) carry nothing and their centroid sits
		// on the outline, where the even-odd test has no defined answer.
		float extentX = 0.0f;
		float extentY = 0.0f;
		for (const ScreenPoint& point : loop)
		{
			extentX = std::max(extentX, std::abs(point.x));
			extentY = std::max(extentY, std::abs(point.y));
		}
		const float extent = std::max(std::max(extentX, extentY), 1.0f);
		const float minDoubleArea = 1e-6f * extent * extent;

		std::vector<int> indices(pointCount);
		const float doubleArea = SignedDoubleArea(loop);
		if (std::abs(doubleArea) <= 1e-9f)
		{
			return false;
		}
		for (size_t i = 0; i < pointCount; ++i)
		{
			indices[i] = static_cast<int>(i);
		}
		if (doubleArea < 0.0f)
		{
			std::reverse(indices.begin(), indices.end());
		}

		const size_t maxIterations = pointCount * pointCount;
		for (size_t iteration = 0; indices.size() > 3 && iteration < maxIterations; ++iteration)
		{
			// Clipping can turn a corner into a point on a straight line; drop
			// those before looking for an ear, otherwise the search stalls.
			bool removedStraightPoint = false;
			for (size_t i = 0; i < indices.size(); ++i)
			{
				const size_t previousIndex = (i + indices.size() - 1) % indices.size();
				const size_t nextIndex = (i + 1) % indices.size();
				if (IsStraightContinuation(
					loop[indices[previousIndex]],
					loop[indices[i]],
					loop[indices[nextIndex]]))
				{
					indices.erase(indices.begin() + static_cast<ptrdiff_t>(i));
					removedStraightPoint = true;
					break;
				}
			}
			if (removedStraightPoint)
			{
				continue;
			}

			// Two passes. The first one refuses any ear that contains another
			// vertex; the second only refuses vertices that are clearly inside, so
			// geometry that is glued to an ear's edge (tangent arcs, duplicated
			// joints) can still be clipped. The result is validated afterwards, so
			// the relaxed pass cannot silently produce a wrong fill.
			bool clipped = false;
			for (int pass = 0; pass < 2 && !clipped; ++pass)
			{
				const float margin = pass == 0 ? 0.0f : 1e-4f;
				for (size_t i = 0; i < indices.size(); ++i)
				{
					const size_t previousIndex = (i + indices.size() - 1) % indices.size();
					const size_t nextIndex = (i + 1) % indices.size();
					const ScreenPoint& a = loop[indices[previousIndex]];
					const ScreenPoint& b = loop[indices[i]];
					const ScreenPoint& c = loop[indices[nextIndex]];

					if (Cross3(a, b, c) <= 0.0f)
					{
						continue;   // reflex corner, not an ear
					}
					bool blocked = false;
					for (const int other : indices)
					{
						if (other == indices[previousIndex]
							|| other == indices[i]
							|| other == indices[nextIndex])
						{
							continue;
						}
						const ScreenPoint& candidate = loop[static_cast<size_t>(other)];
						const bool contains = pass == 0
							? PointInTriangleStrict(candidate, a, b, c)
							: PointInTriangleByMargin(candidate, a, b, c, margin);
						if (contains)
						{
							blocked = true;
							break;
						}
					}
					if (blocked || std::abs(Cross3(a, b, c)) <= minDoubleArea)
					{
						continue;
					}

					p_outTriangles.push_back(a);
					p_outTriangles.push_back(b);
					p_outTriangles.push_back(c);
					indices.erase(indices.begin() + static_cast<ptrdiff_t>(i));
					clipped = true;
					break;
				}
			}
			if (!clipped)
			{
				CORE_WARN(
					"[ScreenPath] ear clipping stalled with {0} of {1} points left",
					indices.size(),
					pointCount);
				return false;
			}
		}

		if (indices.size() == 3)
		{
			const ScreenPoint& a = loop[indices[0]];
			const ScreenPoint& b = loop[indices[1]];
			const ScreenPoint& c = loop[indices[2]];
			if (std::abs(Cross3(a, b, c)) > minDoubleArea)
			{
				p_outTriangles.push_back(a);
				p_outTriangles.push_back(b);
				p_outTriangles.push_back(c);
			}
		}
		return true;
	}

	bool ScreenPath::TriangulateEvenOdd(std::vector<ScreenPoint>& p_outTriangles) const
	{
		p_outTriangles.clear();

		std::vector<int> depths;
		std::vector<int> parents;
		ComputeNesting(depths, parents);
		if (loops.empty())
		{
			return false;
		}

		std::vector<std::vector<ScreenPoint>> cleaned(loops.size());
		for (size_t i = 0; i < loops.size(); ++i)
		{
			if (i < closed.size() && !closed[i])
			{
				continue;
			}
			cleaned[i] = CleanOutline(loops[i]);
		}

		for (size_t index = 0; index < loops.size(); ++index)
		{
			if (cleaned[index].size() < 3 || depths[index] % 2 != 0)
			{
				continue;   // open loop, or a hole handled by its parent
			}

			// Outer polygon counter-clockwise, its holes clockwise: the bridged
			// result is a single keyhole polygon with a consistent winding.
			std::vector<ScreenPoint> merged = AsWinding(cleaned[index], true);
			for (size_t hole = 0; hole < loops.size(); ++hole)
			{
				if (depths[hole] != depths[index] + 1 || parents[hole] != static_cast<int>(index))
				{
					continue;
				}
				const std::vector<ScreenPoint> holeLoop = AsWinding(cleaned[hole], false);
				if (!BridgeHole(merged, holeLoop))
				{
					return false;
				}
			}
			if (!TriangulateSimplePolygon(merged, p_outTriangles))
			{
				return false;
			}
		}
		return !p_outTriangles.empty();
	}

	bool ScreenPath::ValidateTriangulation(const std::vector<ScreenPoint>& p_triangles) const
	{
		if (p_triangles.size() < 3 || p_triangles.size() % 3 != 0)
		{
			return false;
		}

		const ScreenPoint pathSize = Size();
		const float extent = std::max(std::max(pathSize.x, pathSize.y), 1.0f);
		// A correct triangulation has every centroid inside the region. Tangent
		// geometry (an arc running into a cap, as in a slot) leaves sliver
		// triangles whose centroid sits a hair outside - and the even-odd test has
		// no defined answer exactly on the outline. Those are allowed; a triangle
		// that is genuinely outside (the failure mode that made a concave fill
		// cover the convex hull) is far outside this margin.
		const float outsideMargin = std::max(1e-3f, extent * 1e-3f);

		// The real criterion is coverage: the triangles must fill the even-odd
		// region exactly. Counting "n - 2 triangles" is not enough, because
		// collinear points merge into fewer, larger triangles without losing area.
		float triangleArea = 0.0f;
		int outsideCount = 0;
		float worstOutsideDistance = 0.0f;
		for (size_t i = 0; i + 2 < p_triangles.size(); i += 3)
		{
			const ScreenPoint& a = p_triangles[i];
			const ScreenPoint& b = p_triangles[i + 1];
			const ScreenPoint& c = p_triangles[i + 2];
			const ScreenPoint centroid(
				(a.x + b.x + c.x) / 3.0f,
				(a.y + b.y + c.y) / 3.0f);
			if (!ContainsPoint(centroid))
			{
				const float distance = DistanceToOutline(centroid);
				if (distance > outsideMargin)
				{
					++outsideCount;
					worstOutsideDistance = std::max(worstOutsideDistance, distance);
				}
			}
			triangleArea += std::abs(Cross3(a, b, c)) * 0.5f;
		}

		std::vector<int> depths;
		std::vector<int> parents;
		ComputeNesting(depths, parents);
		float expectedArea = 0.0f;
		for (size_t i = 0; i < loops.size(); ++i)
		{
			if (i < closed.size() && !closed[i])
			{
				continue;
			}
			const float area = std::abs(SignedDoubleArea(loops[i])) * 0.5f;
			if (depths[i] % 2 == 0)
			{
				expectedArea += area;
			}
			else
			{
				expectedArea -= area;   // a hole
			}
		}
		const float tolerance = std::max(1e-3f, std::abs(expectedArea) * 1e-3f);
		const bool areaIsRight = std::abs(triangleArea - expectedArea) <= tolerance;
		if (outsideCount > 0 || !areaIsRight)
		{
			CORE_WARN(
				"[ScreenPath] validation failed: {0} triangles, area {1:.3f} vs expected {2:.3f} "
				"(tolerance {3:.3f}), {4} centroids outside by more than {5:.4f} px (worst {6:.4f})",
				p_triangles.size() / 3,
				triangleArea,
				expectedArea,
				tolerance,
				outsideCount,
				outsideMargin,
				worstOutsideDistance);

			// The centroid test and the triangle area can only disagree when the
			// outline crosses itself, so say so explicitly instead of leaving the
			// next reader to guess.
			for (size_t i = 0; i < loops.size(); ++i)
			{
				if (i < closed.size() && !closed[i])
				{
					continue;
				}
				std::vector<ScreenPoint> firstCrossings;
				const int selfIntersections = CountSelfIntersections(
					loops[i], firstCrossings);
				if (selfIntersections > 0)
				{
					CORE_WARN(
						"[ScreenPath] loop {0} self intersects {1} times; first crossing "
						"between ({2:.2f}, {3:.2f}) and ({4:.2f}, {5:.2f})",
						i,
						selfIntersections,
						firstCrossings[0].x,
						firstCrossings[0].y,
						firstCrossings[1].x,
						firstCrossings[1].y);
				}
				else
				{
					CORE_WARN(
						"[ScreenPath] loop {0} has no self intersection: the outline is "
						"simple, so the triangulation itself is at fault",
						i);
				}
			}
			return false;
		}
		return true;
	}

	const std::vector<ScreenPoint>& ScreenPath::GetFillTriangles() const
	{
		if (mFillTrianglesValid)
		{
			return mFillTriangles;
		}
		mFillTrianglesValid = true;
		mFillTriangles.clear();

		if (!TriangulateEvenOdd(mFillTriangles))
		{
			// Say what the geometry looked like, otherwise "no fill" is silent and
			// impossible to diagnose from the outside.
			std::vector<int> depths;
			std::vector<int> parents;
			ComputeNesting(depths, parents);
			std::string summary;
			for (size_t i = 0; i < loops.size(); ++i)
			{
				summary += " loop" + std::to_string(i)
					+ " pts=" + std::to_string(loops[i].size())
					+ " closed=" + std::to_string(
						(i < closed.size() && closed[i]) ? 1 : 0)
					+ " depth=" + std::to_string(i < depths.size() ? depths[i] : -1)
					+ " area=" + std::to_string(
						std::abs(SignedDoubleArea(loops[i])) * 0.5f);
			}
			CORE_WARN("[ScreenPath] fill triangulation failed;{0}", summary);
			DumpForDebug();
			mFillTriangles.clear();
			return mFillTriangles;
		}
		if (!ValidateTriangulation(mFillTriangles))
		{
			CORE_WARN(
				"[ScreenPath] fill triangulation rejected for a {0} loop outline "
				"({1} triangles produced); the shape is drawn as an outline only",
				loops.size(),
				mFillTriangles.size() / 3);
			DumpForDebug();
			mFillTriangles.clear();
		}
		return mFillTriangles;
	}

	void ScreenPath::DumpForDebug() const
	{
		// Written next to the executable so a rejected loop can be analysed
		// offline instead of asking anyone to copy a hundred points by hand.
		const std::string path =
			Tools::Utils::PathParser::GetExeDirectory() + "/screenpath_dump.txt";
		std::ofstream file(path, std::ios::app);
		if (!file.is_open())
		{
			return;
		}
		file << "=== path loops=" << loops.size() << " ===\n";
		for (size_t i = 0; i < loops.size(); ++i)
		{
			const bool isClosed = i < closed.size() && closed[i];
			file << "loop " << i
				<< " closed=" << (isClosed ? 1 : 0)
				<< " points=" << loops[i].size()
				<< " area=" << (std::abs(SignedDoubleArea(loops[i])) * 0.5f)
				<< "\n";
			for (const ScreenPoint& point : loops[i])
			{
				file << point.x << " " << point.y << "\n";
			}
		}
		file << "=== end ===\n";
	}
}
