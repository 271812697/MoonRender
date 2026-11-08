#include "Interactive/Gizmo/CurveEditGizmo.h"

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
	CurveEditGizmo::CurveEditGizmo(ImRenderer& p_renderer) : m_renderer(p_renderer) { }

	bool CurveEditGizmo::edit(unsigned int _id, Eigen::Vector3f& _normal, Eigen::Vector3f& _origin, std::vector<Eigen::Vector3f>& polygon)
	{
		FrameParam& cameraParam = m_renderer.getFrameParam();
		Gizmo::DrawRayHitScreenPoint(m_renderer);
		bool ret = false;
		Eigen::Vector3f outNormal = Eigen::Vector3f(_normal);
		Eigen::Vector3f outOrigin = Eigen::Vector3f(_origin);
		Eigen::Vector3f normal = outNormal;
		Eigen::Vector3f planeOrigin = outOrigin;

		unsigned int planeArrow = m_renderer.makeId("planeArrow");
		unsigned int planeOriginId = m_renderer.makeId("planeOrigin");
		unsigned int planeOriginCircle = m_renderer.makeId("planeCircle");

		// follow camera
		outNormal = -cameraParam.viewDirectioin.normalized();

		Eigen::Vector3f mouse = cameraParam.rayOrigin + cameraParam.rayDirection;
		float wordSize = m_renderer.pixelsToWorldSize(mouse, 3);
		float worldHeight = m_renderer.pixelsToWorldSize(planeOrigin, 50);
		float cirleDetectRadius = m_renderer.pixelsToWorldSize(planeOrigin, 40);


		// colorStack.push_back(Color_Red);
		 //drawCircleFilled(mouse, cameraParam.viewDirectioin, wordSize, 20);
		// colorStack.pop_back();
		// normal = *outNormal;

		 //ret |= gizmoSphereAxisTranslationBehavior(planeOriginId, planeOrigin, pixelsToWorldSize(planeOrigin, 15), normal, cameraParam.snapTranslation, outOrigin);
		 //gizmoSpherePlaneTranslationBehavior(
		   //  planeOriginId, planeOrigin, pixelsToWorldSize(planeOrigin, 10), normal, cameraParam.snapTranslation, outOrigin);
		// gizmoCircleAxisTranslationBehavior(planeOriginCircle, planeOrigin, cirleDetectRadius, normal, cameraParam.snapTranslation, outOrigin);
		// gizmoOperateNormalBehavior(planeArrow, planeOrigin, planeOrigin + normal * worldHeight, pixelsToWorldSize(planeOrigin, 3), outNormal);

		planeOrigin = outOrigin;
		/*
		if( wasKeyReleased(ActionSelect) && isKeyDown(ActionControl) )
		{
			Ray ray(cameraParam.rayOrigin, cameraParam.rayDirection);
			float t0 = FLT_MAX;
			if( Intersect(ray, Plane(normal, planeOrigin), t0) )
			{
				 Eigen::Vector3f intersectPoint = ray.m_origin + ray.m_direction * t0;
				 polygon.push_back(intersectPoint);
				 outOrigin = intersectPoint;
				 ret = true;
			}
		}

		*/


		std::vector<unsigned int> polygonId(polygon.size());
		for (int i = 0; i < polygon.size(); i++)
		{
			char id[20] = "\0";
			sprintf(id, "Polygon%d", i);
			polygonId[i] = m_renderer.makeId(id);
		}
		for (int i = 0; i < polygon.size(); i++)
		{
			Eigen::Vector3f pos = polygon[i];
			//ret|=gizmoPlaneTranslationBehavior(polygonId[i], polygon[i],normal, cameraParam.snapTranslation,pixelsToWorldSize(pos, 15), &pos);
			polygon[i] = pos;
		}
		/*
		Draw plane grid
		drawPlaneGrid(planeOrigin,normal,5,5);

		drawArrow(planeArrow, planeOrigin, normal, worldHeight, Color_Green);
		drawPoint(planeOrigin, 10, planeOriginId == hotId ? Color_Gold:Color_White );
		alphaStack.push_back(0.5);
		colorStack.push_back(planeOriginCircle == hotId ? Color_Gold : Color_Green);
		drawCircleFilled(planeOrigin, normal, cirleDetectRadius, 20);
		sizeStack.push_back(3.0);
		colorStack.push_back( Color_Green);
		drawCircle(planeOrigin, normal, cirleDetectRadius, 20);
		sizeStack.pop_back();
		alphaStack.pop_back();
		colorStack.pop_back();
		colorStack.pop_back();
		*/

		for (int i = 0; i < polygon.size(); i++)
		{
			if (i > 0)
			{
				//drawLine(polygon[i], polygon[i - 1], 2.0, Color_Blue);
			}
			m_renderer.drawPoint(polygon[i], 7, polygonId[i] == m_renderer.getHotId() ? Color_Gold : Color_Green);
			if (polygonId[i] == m_renderer.getHotId())
			{
				m_renderer.pushColor(Color_Gold);
				m_renderer.drawCircle(polygon[i], cameraParam.viewDirectioin, m_renderer.pixelsToWorldSize(polygon[i], 10), 20);
				m_renderer.popColor();
				m_renderer.pushAlpha(0.3);
				m_renderer.pushColor(Color_Red);
				m_renderer.drawCircleFilled(polygon[i], cameraParam.viewDirectioin, m_renderer.pixelsToWorldSize(polygon[i], 10), 20);
				m_renderer.popAlpha();
				m_renderer.popColor();
			}
		}

		if (polygon.size() > 0)
		{
			Ray ray(cameraParam.rayOrigin, cameraParam.rayDirection);
			float t0 = FLT_MAX;
			if (Intersect(ray, Plane(normal, planeOrigin), t0))
			{
				Eigen::Vector3f last = polygon.back();
				Eigen::Vector3f dir = (ray.m_origin + ray.m_direction * t0 - last).normalized();
				Gizmo::DrawAxisArrow(m_renderer, m_renderer.makeId("NoId"), last, dir, m_renderer.pixelsToWorldSize(last, 30), Color_Gold);
			}
		}
		_normal = outNormal;
		_origin = outOrigin;
		return ret;
	}
	bool CurveEditGizmo::edit(unsigned int _id, Eigen::Vector3f& _normal, Eigen::Vector3f& _origin, std::vector<std::pair<Eigen::Vector3f, Eigen::Vector3f>>& polygon)
	{
		FrameParam& cameraParam = m_renderer.getFrameParam();
		bool ret = false;
		Eigen::Vector3f* outNormal = new Eigen::Vector3f(_normal);
		Eigen::Vector3f* outOrigin = new Eigen::Vector3f(_origin);
		Eigen::Vector3f normal = *outNormal;
		Eigen::Vector3f planeOrigin = *outOrigin;

		unsigned int planeArrow = m_renderer.makeId("planeArrow");
		unsigned int planeOriginId = m_renderer.makeId("planeOrigin");
		unsigned int planeOriginCircle = m_renderer.makeId("planeCircle");

		// follow camera
		//*outNormal = -cameraParam.viewDirectioin.normalized();
		Eigen::Vector3f mouse = cameraParam.rayOrigin + cameraParam.rayDirection;
		float wordSize = m_renderer.pixelsToWorldSize(mouse, 3);
		float worldHeight = m_renderer.pixelsToWorldSize(planeOrigin, 50);
		float cirleDetectRadius = m_renderer.pixelsToWorldSize(planeOrigin, 40);

		m_renderer.pushColor(Color_Red);
		m_renderer.drawCircleFilled(mouse, cameraParam.viewDirectioin, wordSize, 20);
		m_renderer.popColor();
		// normal = *outNormal;

		// ret |= gizmoSphereAxisTranslationBehavior(planeOriginId, planeOrigin, pixelsToWorldSize(planeOrigin, 15), normal,
		// cameraParam.snapTranslation, outOrigin);
		m_drag.spherePlaneTranslation(m_renderer, 
			planeOriginId, planeOrigin, m_renderer.pixelsToWorldSize(planeOrigin, 10), normal, cameraParam.snapTranslation, outOrigin);
		m_drag.circleAxisTranslation(m_renderer, 
			planeOriginCircle, planeOrigin, cirleDetectRadius, normal, cameraParam.snapTranslation, outOrigin);
		m_drag.operateNormal(m_renderer, 
			planeArrow, planeOrigin, planeOrigin + normal * worldHeight, m_renderer.pixelsToWorldSize(planeOrigin, 3), outNormal);

		planeOrigin = *outOrigin;
		Eigen::Vector3f Up = abs(normal.y()) > 0.99 ? Eigen::Vector3f(1, 0, 0) : Eigen::Vector3f(0, 1, 0);
		Eigen::Vector3f xaxis = normal.cross(Up).normalized();    // Normalize(Cross(normal, Up));
		Eigen::Vector3f zaxis = xaxis.cross(normal).normalized(); // Normalize(Cross(xaxis, normal));

		if (m_renderer.wasKeyReleased(ActionSelect) && m_renderer.isKeyDown(ActionControl))
		{
			Ray ray(cameraParam.rayOrigin, cameraParam.rayDirection);
			float t0 = FLT_MAX;
			if (Intersect(ray, Plane(normal, planeOrigin), t0))
			{
				Eigen::Vector3f intersectPoint = ray.m_origin + ray.m_direction * t0;
				Eigen::Vector3f tangent = polygon.empty() ? xaxis : (intersectPoint - polygon.back().first).normalized();
				polygon.push_back({ intersectPoint, tangent });
				ret = true;
			}
		}
		std::vector<std::pair<unsigned int, unsigned int>> polygonId(polygon.size());
		for (int i = 0; i < polygon.size(); i++)
		{
			char id[20] = "\0";
			char id1[20] = "\0";
			sprintf(id, "Polygon%d", i);
			sprintf(id1, "Tangent%d", i);
			polygonId[i].first = m_renderer.makeId(id);
			polygonId[i].second = m_renderer.makeId(id1);

		}
		for (int i = 0; i < polygon.size(); i++)
		{
			Eigen::Vector3f pos = polygon[i].first;
			//me polygon's points
			ret |= m_drag.planeTranslation(m_renderer, polygonId[i].first, polygon[i].first, normal, cameraParam.snapTranslation,
				m_renderer.pixelsToWorldSize(pos, 15), &pos);
			polygon[i].first = pos;
			pos = polygon[i].first + polygon[i].second;
			//me tangent (normal)
			if (m_drag.planeTranslation(m_renderer, polygonId[i].second, pos, normal, cameraParam.snapTranslation, m_renderer.pixelsToWorldSize(pos, 15), &pos))
			{
				ret |= true;
			}

			polygon[i].second = pos - polygon[i].first;

		}

		// Draw plane grid
		m_renderer.drawPlaneGrid(planeOrigin, normal, 5, 5);

		Gizmo::DrawAxisArrow(m_renderer, planeArrow, planeOrigin, normal, worldHeight, Color_Green);
		m_renderer.drawPoint(planeOrigin, 10, planeOriginId == m_renderer.getHotId() ? Color_Gold : Color_White);
		m_renderer.pushAlpha(0.5);
		m_renderer.pushColor(planeOriginCircle == m_renderer.getHotId() ? Color_Gold : Color_Green);
		m_renderer.drawCircleFilled(planeOrigin, normal, cirleDetectRadius, 20);
		m_renderer.pushSize(3.0);
		m_renderer.pushColor(Color_Green);
		m_renderer.drawCircle(planeOrigin, normal, cirleDetectRadius, 20);
		m_renderer.popSize();
		m_renderer.popAlpha();
		m_renderer.popColor();
		m_renderer.popColor();
		for (int i = 0; i < polygon.size(); i++)
		{
			if (i > 0)
			{
				m_renderer.drawLine(polygon[i].first, polygon[i - 1].first, 2.0, Color_Blue);
			}
			m_renderer.drawPoint(polygon[i].first, 10, polygonId[i].first == m_renderer.getHotId() ? Color_Gold : Color_Green);
			m_renderer.drawPoint(polygon[i].first + polygon[i].second, 10, (polygonId[i].first == m_renderer.getHotId() || polygonId[i].second == m_renderer.getHotId()) ? Color_Gold : Color_Red);
			m_renderer.drawPoint(polygon[i].first - polygon[i].second, 10, (polygonId[i].first == m_renderer.getHotId() || polygonId[i].second == m_renderer.getHotId()) ? Color_Gold : Color_Brown);
			m_renderer.drawLine(polygon[i].first, polygon[i].first + polygon[i].second, 2.0, Color_White);
			m_renderer.drawLine(polygon[i].first, polygon[i].first - polygon[i].second, 2.0, Color_White);

			if (polygonId[i].first == m_renderer.getHotId())
			{
				m_renderer.pushColor(Color_Gold);
				m_renderer.drawCircle(polygon[i].first, cameraParam.viewDirectioin, m_renderer.pixelsToWorldSize(polygon[i].first, 10), 20);
				m_renderer.popColor();
				m_renderer.pushAlpha(0.3);
				m_renderer.pushColor(Color_Red);
				m_renderer.drawCircleFilled(polygon[i].first, cameraParam.viewDirectioin, m_renderer.pixelsToWorldSize(polygon[i].first, 10), 20);
				m_renderer.popAlpha();
				m_renderer.popColor();
			}
		}
		//Draw Arrow between last point and hit point
		if (polygon.size() > 0)
		{
			Ray ray(cameraParam.rayOrigin, cameraParam.rayDirection);
			float t0 = FLT_MAX;
			if (Intersect(ray, Plane(normal, planeOrigin), t0))
			{
				Eigen::Vector3f last = polygon.back().first;
				Eigen::Vector3f dir = (ray.m_origin + ray.m_direction * t0 - last).normalized();
				Gizmo::DrawAxisArrow(m_renderer, m_renderer.makeId("NoId"), last, dir, m_renderer.pixelsToWorldSize(last, 30), Color_Gold);
			}
		}
		_normal = *outNormal;
		_origin = *outOrigin;
		delete outNormal;
		delete outOrigin;
		return ret;
	}
}
