#include "Interactive/Gizmo/PrismEditGizmo.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <vector>
#include <Eigen/Core>

#include "Interactive/Im3DRenderer.h"
#include "Interactive/Im3DPalette.h"
#include "Interactive/MathUtil/MathUtil.h"
#include "Interactive/Gizmo/GizmoCommon.h"

namespace MOON
{
	PrismEditGizmo::PrismEditGizmo(ImRenderer& p_renderer) : m_renderer(p_renderer) { }

	bool PrismEditGizmo::edit(unsigned int _id, Eigen::Vector3f& _normal, Eigen::Vector3f& _origin, float* _height, std::vector<Eigen::Vector3f>& polygon, std::vector<Eigen::Vector2f>& polygon2D, bool flip)
	{
		FrameParam& cameraParam = m_renderer.getFrameParam();
		bool ret = false;
		Eigen::Vector3f* outNormal = new Eigen::Vector3f(_normal);
		Eigen::Vector3f* outOrigin = new Eigen::Vector3f(_origin);
		Eigen::Vector3f normal = *outNormal;
		Eigen::Vector3f planeOrigin = *outOrigin;
		float height = *_height;

		unsigned int planeArrow = m_renderer.makeId("planeArrow");
		unsigned int planeOriginId = m_renderer.makeId("planeOrigin");
		unsigned int polyBaryTop = m_renderer.makeId("polyBaryTop");
		unsigned int polyBaryBottom = m_renderer.makeId("polyBaryBottom");

		Eigen::Vector3f mouse = cameraParam.rayOrigin + cameraParam.rayDirection;
		float wordSize = m_renderer.pixelsToWorldSize(mouse, 3);
		float worldHeight = m_renderer.pixelsToWorldSize(planeOrigin, 50);

		ret |= m_drag.sphereAxisTranslation(m_renderer, planeOriginId, planeOrigin, m_renderer.pixelsToWorldSize(planeOrigin, 15), normal,
			cameraParam.snapTranslation, outOrigin);
		planeOrigin = *outOrigin;
		ret |= m_drag.operateNormal(m_renderer, 
			planeArrow, planeOrigin, planeOrigin + normal * worldHeight, m_renderer.pixelsToWorldSize(planeOrigin, 3), outNormal);

		m_renderer.pushColor(Color_Red);
		m_renderer.drawCircleFilled(mouse, cameraParam.viewDirectioin, wordSize, 20);
		m_renderer.popColor();
		normal = *outNormal;
		Eigen::Vector3f Up = abs(normal.y()) > 0.99 ? Eigen::Vector3f(1, 0, 0) : Eigen::Vector3f(0, 1, 0);
		Eigen::Vector3f xaxis = normal.cross(Up).normalized();//Normalize(Cross(normal, Up));
		Eigen::Vector3f zaxis = xaxis.cross(normal).normalized();//Normalize(Cross(xaxis, normal));

		if (m_renderer.wasKeyReleased(ActionSelect) && m_renderer.isKeyDown(ActionControl))
		{
			Ray ray(cameraParam.rayOrigin, cameraParam.rayDirection);
			float t0 = FLT_MAX;
			if (Intersect(ray, Plane(normal, planeOrigin), t0))
			{
				ret = true;
				Eigen::Vector3f intersectPoint = ray.m_origin + ray.m_direction * t0 - planeOrigin;
				polygon2D.push_back(Eigen::Vector2f(intersectPoint.dot(xaxis), intersectPoint.dot(zaxis)));
			}
		}
		Gizmo::DrawAxisArrow(m_renderer, planeArrow, planeOrigin, normal, worldHeight, Color_Green);
		m_renderer.drawPoint(planeOrigin, 15, planeOriginId == m_renderer.getHotId() ? Color_Gold : Color_White);
		for (int i = 0; i < polygon2D.size(); i++)
		{
			int j = (i + 1) % polygon2D.size();
			if (flip)
			{
				m_renderer.drawLine(xaxis * polygon2D[i].x() + zaxis * polygon2D[i].y() + planeOrigin,
					xaxis * polygon2D[i].x() + zaxis * polygon2D[i].y() + planeOrigin + normal * height, 2.0, Color_Blue);
				m_renderer.drawLine(xaxis * polygon2D[i].x() + zaxis * polygon2D[i].y() + planeOrigin,
					xaxis * polygon2D[j].x() + zaxis * polygon2D[j].y() + planeOrigin, 2.0, Color_Blue);
				m_renderer.drawLine(xaxis * polygon2D[i].x() + zaxis * polygon2D[i].y() + planeOrigin + normal * height,
					xaxis * polygon2D[j].x() + zaxis * polygon2D[j].y() + planeOrigin + normal * height, 2.0, Color_Blue);
			}
			else
			{
				m_renderer.drawLine(xaxis * polygon2D[i].x() + zaxis * polygon2D[i].y() + planeOrigin,
					xaxis * polygon2D[i].x() + zaxis * polygon2D[i].y() + planeOrigin - normal * height, 2.0, Color_Blue);
				m_renderer.drawLine(xaxis * polygon2D[i].x() + zaxis * polygon2D[i].y() + planeOrigin,
					xaxis * polygon2D[j].x() + zaxis * polygon2D[j].y() + planeOrigin,
					2.0, Color_Blue);
				m_renderer.drawLine(xaxis * polygon2D[i].x() + zaxis * polygon2D[i].y() + planeOrigin - normal * height,
					xaxis * polygon2D[j].x() + zaxis * polygon2D[j].y() + planeOrigin - normal * height, 2.0, Color_Blue);
			}

		}
		Eigen::Vector3f center(0, 0, 0);

		for (const auto& point : polygon2D)
		{
			Eigen::Vector3f temp = xaxis * point.x() + zaxis * point.y() + planeOrigin;
			center = center + temp;
			m_renderer.drawPoint(temp, 10, Color_Green);
			m_renderer.drawPoint(flip ? Eigen::Vector3f(temp + (normal * height)) : temp - normal * height, 10, Color_Green);
		}
		if (polygon2D.size() > 0)
		{
			Ray ray(cameraParam.rayOrigin, cameraParam.rayDirection);
			float t0 = FLT_MAX;
			if (Intersect(ray, Plane(normal, planeOrigin), t0))
			{
				Eigen::Vector3f last = xaxis * polygon2D.back().x() + zaxis * polygon2D.back().y() + planeOrigin;
				Eigen::Vector3f dir = (ray.m_origin + ray.m_direction * t0 - last).normalized();

				Gizmo::DrawAxisArrow(m_renderer, m_renderer.makeId("No unsigned int"), last, dir, m_renderer.pixelsToWorldSize(last, 30), Color_Gold);
			}
		}
		if (polygon2D.size() > 2)
		{
			center = center / polygon2D.size();
			m_renderer.drawPoint(center, 10, Color_Brown);
			m_renderer.drawPoint(flip ? Eigen::Vector3f(center + normal * height) : center - normal * height, 10, Color_Brown);
			Gizmo::DrawAxisArrow(m_renderer, polyBaryTop, center, flip ? -normal : normal, worldHeight, Color_Green);
			Gizmo::DrawAxisArrow(m_renderer, polyBaryBottom, flip ? Eigen::Vector3f(center + normal * height) : center - normal * height, flip ? normal : -normal,
				worldHeight, Color_Green);

			Eigen::Vector3f s = *outOrigin;
			if (m_drag.axisTranslation(m_renderer, polyBaryTop, center, flip ? -normal : normal, cameraParam.snapTranslation,
				m_renderer.pixelsToWorldSize(center, 50), m_renderer.pixelsToWorldSize(center, 3), outOrigin))
			{
				s = *outOrigin - s;
				*_height = *_height + s.dot(flip ? -normal : normal);
				ret = true;
			}
			else
			{
				Eigen::Vector3f start = *outOrigin;
				Eigen::Vector3f en = *outOrigin;
				if (m_drag.axisTranslation(m_renderer, polyBaryBottom, flip ? Eigen::Vector3f(center + normal * height) : center - normal * height,
					flip ? normal : -normal,
					cameraParam.snapTranslation, m_renderer.pixelsToWorldSize(center, 50), m_renderer.pixelsToWorldSize(center, 3),
					&en))
				{
					en = en - start;
					*_height = *_height + en.dot(flip ? normal : -normal);
					ret = true;
				}
			}
			*_height = std::fmax(0, *_height);
		}
		_normal = *outNormal;
		_origin = *outOrigin;
		delete outNormal;
		delete outOrigin;

		if (ret)
		{
			polygon.clear();
			for (const auto& point : polygon2D)
			{
				polygon.push_back(xaxis * point.x() + zaxis * point.y() + planeOrigin);
			}
		}
		return ret;
	}
}
