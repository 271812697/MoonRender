#include "Interactive/Screen/HitShape.h"
#include "Interactive/Screen/ScreenPath.h"
#include <algorithm>
#include <cmath>

namespace MOON
{
	namespace
	{
		constexpr float kPi = 3.14159265358979f;
		constexpr float kDegToRad = kPi / 180.0f;

		float Cross2D(const ImVec2& p_a, const ImVec2& p_b, const ImVec2& p_c)
		{
			return (p_b.x - p_a.x) * (p_c.y - p_a.y)
				- (p_b.y - p_a.y) * (p_c.x - p_a.x);
		}

		/** Shortest signed difference between two angles, in degrees. */
		float AngleDifferenceDeg(float p_a, float p_b)
		{
			float diff = fmodf(p_a - p_b + 540.0f, 360.0f) - 180.0f;
			return diff;
		}

		ImVec2 Centroid(const std::vector<ImVec2>& p_points)
		{
			ImVec2 sum(0.0f, 0.0f);
			for (const ImVec2& point : p_points)
			{
				sum.x += point.x;
				sum.y += point.y;
			}
			const float count = static_cast<float>(std::max<size_t>(p_points.size(), 1));
			return ImVec2(sum.x / count, sum.y / count);
		}

		/** Grows a convex outline by p_pad pixels around its centroid. */
		std::vector<ImVec2> PaddedOutline(
			const std::vector<ImVec2>& p_points,
			const ImVec2& p_center,
			float p_pad)
		{
			std::vector<ImVec2> outline;
			outline.reserve(p_points.size());
			if (p_pad <= 0.0f)
			{
				return p_points;
			}

			float maxDistance = 0.0f;
			for (const ImVec2& point : p_points)
			{
				maxDistance = std::max(
					maxDistance,
					std::sqrt(
						(point.x - p_center.x) * (point.x - p_center.x)
						+ (point.y - p_center.y) * (point.y - p_center.y)));
			}
			if (maxDistance <= 0.0001f)
			{
				return p_points;
			}

			const float grow = 1.0f + p_pad / maxDistance;
			for (const ImVec2& point : p_points)
			{
				outline.push_back(ImVec2(
					p_center.x + (point.x - p_center.x) * grow,
					p_center.y + (point.y - p_center.y) * grow));
			}
			return outline;
		}

		bool PointInConvexOutline(const ImVec2& p_point, const std::vector<ImVec2>& p_outline)
		{
			if (p_outline.size() < 3)
			{
				return false;
			}
			bool hasNegative = false;
			bool hasPositive = false;
			for (size_t i = 0; i < p_outline.size(); ++i)
			{
				const ImVec2& a = p_outline[i];
				const ImVec2& b = p_outline[(i + 1) % p_outline.size()];
				const float side = Cross2D(a, b, p_point);
				hasNegative |= side < 0.0f;
				hasPositive |= side > 0.0f;
			}
			// All cross products on the same side means inside; this does not
			// depend on the winding order the widget used.
			return !(hasNegative && hasPositive);
		}
	}

	ImVec2 HitShape::LocalCenter() const
	{
		switch (type)
		{
		case EType::Rect:
		case EType::Circle:
		case EType::RingArc:
			return center;
		case EType::Path:
			return path ? path->Center() : ImVec2(0.0f, 0.0f);
		default:
			return points.empty() ? ImVec2(0.0f, 0.0f) : Centroid(points);
		}
	}

	bool HitShape::Contains(const ImVec2& p_local) const
	{
		switch (type)
		{
		case EType::Rect:
			return std::abs(p_local.x - center.x) <= halfExtent.x + pad
				&& std::abs(p_local.y - center.y) <= halfExtent.y + pad;

		case EType::Circle:
		{
			const float dx = p_local.x - center.x;
			const float dy = p_local.y - center.y;
			const float reach = radius + pad;
			return dx * dx + dy * dy <= reach * reach;
		}

		case EType::RingArc:
		{
			const float dx = p_local.x - center.x;
			const float dy = p_local.y - center.y;
			const float distance = std::sqrt(dx * dx + dy * dy);
			if (distance < radius - bandHalfWidth - pad
				|| distance > radius + bandHalfWidth + pad)
			{
				return false;
			}
			// The angular slack translates the pixel pad into an arc length, so
			// the pick area grows the same way along the whole band.
			const float slackDeg = std::atan2(pad, std::max(distance, 0.0001f)) / kDegToRad;
			const float angleDeg = std::atan2(dy, dx) / kDegToRad;
			const float differenceDeg = AngleDifferenceDeg(angleDeg, startAngleDeg);
			if (sweepAngleDeg >= 0.0f)
			{
				return differenceDeg >= -slackDeg && differenceDeg <= sweepAngleDeg + slackDeg;
			}
			return differenceDeg <= slackDeg && differenceDeg >= sweepAngleDeg - slackDeg;
		}

		case EType::Triangle:
		case EType::Polygon:
		{
			if (points.size() < 3)
			{
				return false;
			}
			const std::vector<ImVec2> outline = PaddedOutline(points, LocalCenter(), pad);
			return PointInConvexOutline(p_local, outline);
		}

		case EType::Path:
		{
			if (!path || path->IsEmpty())
			{
				return false;
			}
			const bool hitFill =
				(pickMode != EPickMode::Stroke) && path->ContainsPointWithPad(p_local, pad);
			const bool hitStroke =
				(pickMode != EPickMode::Fill)
				&& path->NearOutline(p_local, strokeWidth * 0.5f + strokePickSlack + pad);
			return hitFill || hitStroke;
		}
		}
		return false;
	}

	void HitShape::Draw(
		ImDrawList* p_drawList,
		const ImVec2& p_offset,
		ImU32 p_fill,
		ImU32 p_outline,
		float p_outlineWidth) const
	{
		if (p_drawList == nullptr)
		{
			return;
		}

		switch (type)
		{
		case EType::Rect:
		{
			const ImVec2 min(p_offset.x + center.x - halfExtent.x, p_offset.y + center.y - halfExtent.y);
			const ImVec2 max(p_offset.x + center.x + halfExtent.x, p_offset.y + center.y + halfExtent.y);
			if (p_fill != 0)
			{
				p_drawList->AddRectFilled(min, max, p_fill);
			}
			if (p_outlineWidth > 0.0f)
			{
				// Argument order is explicit: rounding, thickness, flags. The two
				// ImDrawList implementations in the tree differ here, and the
				// swapped-meaning legacy overloads are easy to bind to by accident.
				p_drawList->AddRect(min, max, p_outline, 0.0f, p_outlineWidth, 0);
			}
			break;
		}

		case EType::Circle:
		{
			const ImVec2 point(p_offset.x + center.x, p_offset.y + center.y);
			if (p_fill != 0)
			{
				p_drawList->AddCircleFilled(point, radius, p_fill);
			}
			if (p_outlineWidth > 0.0f)
			{
				p_drawList->AddCircle(point, radius, p_outline, 0, p_outlineWidth);
			}
			break;
		}

		case EType::RingArc:
		{
			if (bandHalfWidth <= 0.0f)
			{
				break;
			}
			// Approximate the band with a polyline of the given thickness.
			const int segmentCount = std::max(6, static_cast<int>(std::ceil(std::abs(sweepAngleDeg) / 5.0f)));
			std::vector<ImVec2> arcPoints;
			arcPoints.reserve(segmentCount + 1);
			const float startRad = startAngleDeg * kDegToRad;
			const float sweepRad = sweepAngleDeg * kDegToRad;
			for (int i = 0; i <= segmentCount; ++i)
			{
				const float t = static_cast<float>(i) / static_cast<float>(segmentCount);
				const float angle = startRad + sweepRad * t;
				arcPoints.push_back(ImVec2(
					p_offset.x + center.x + radius * cosf(angle),
					p_offset.y + center.y + radius * sinf(angle)));
			}
			p_drawList->AddPolyline(
				arcPoints.data(),
				static_cast<int>(arcPoints.size()),
				p_fill != 0 ? p_fill : p_outline,
				bandHalfWidth * 2.0f,
				0);
			break;
		}

		case EType::Triangle:
		case EType::Polygon:
		{
			if (points.size() < 3)
			{
				break;
			}
			std::vector<ImVec2> outline;
			outline.reserve(points.size());
			for (const ImVec2& point : points)
			{
				outline.push_back(ImVec2(p_offset.x + point.x, p_offset.y + point.y));
			}
			if (p_fill != 0)
			{
				p_drawList->AddConvexPolyFilled(outline.data(), static_cast<int>(outline.size()), p_fill);
			}
			if (p_outlineWidth > 0.0f)
			{
				p_drawList->AddPolyline(
					outline.data(),
					static_cast<int>(outline.size()),
					p_outline,
					p_outlineWidth,
					ImDrawFlags_Closed);
			}
			break;
		}

		case EType::Path:
		{
			if (!path || path->IsEmpty())
			{
				break;
			}

			// The fill comes from ScreenPath's even-odd triangulation, which
			// handles concave outlines and holes; the draw list's own concave fill
			// is not used (see ScreenPath::GetFillTriangles).
			const bool wantFill = pickMode != EPickMode::Stroke && p_fill != 0;
			// In Stroke mode the outline *is* the shape, so it takes the main
			// color (the one that changes on hover / press).
			const bool strokeIsMainColor = pickMode == EPickMode::Stroke;
			const ImU32 strokeColor = strokeIsMainColor ? p_fill : p_outline;
			const float strokeThickness = strokeIsMainColor
				? std::max(strokeWidth, 1.0f)
				: p_outlineWidth;

			if (wantFill)
			{
				const std::vector<ImVec2>& triangles = path->GetFillTriangles();
				for (size_t t = 0; t + 2 < triangles.size(); t += 3)
				{
					p_drawList->AddTriangleFilled(
						ImVec2(p_offset.x + triangles[t].x, p_offset.y + triangles[t].y),
						ImVec2(p_offset.x + triangles[t + 1].x, p_offset.y + triangles[t + 1].y),
						ImVec2(p_offset.x + triangles[t + 2].x, p_offset.y + triangles[t + 2].y),
						p_fill);
				}
			}

			std::vector<ImVec2> scratch;
			for (size_t loopIndex = 0; loopIndex < path->loops.size(); ++loopIndex)
			{
				const std::vector<ImVec2>& loop = path->loops[loopIndex];
				if (loop.size() < 2)
				{
					continue;
				}
				scratch.clear();
				scratch.reserve(loop.size());
				for (const ImVec2& point : loop)
				{
					scratch.push_back(ImVec2(p_offset.x + point.x, p_offset.y + point.y));
				}

				if (strokeColor != 0 && strokeThickness > 0.0f)
				{
					const bool loopClosed =
						loopIndex < path->closed.size() && path->closed[loopIndex];
					p_drawList->AddPolyline(
						scratch.data(),
						static_cast<int>(scratch.size()),
						strokeColor,
						strokeThickness,
						loopClosed ? ImDrawFlags_Closed : 0);
				}
			}
			break;
		}
		}
	}
}
