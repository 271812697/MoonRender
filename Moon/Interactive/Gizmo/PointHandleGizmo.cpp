#include "Interactive/Gizmo/PointHandleGizmo.h"

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
#include <Rendering/Overlay/Im2DRender.h>
#include <Rendering/Overlay/Im2D.h>
#include <ThirdParty/imgui/imgui/imgui.h>

namespace MOON
{
	PointHandleGizmo::PointHandleGizmo(ImRenderer& p_renderer) : m_renderer(p_renderer) { }

	void PointHandleGizmo::drawRayHitScreenPoint()
	{
		Gizmo::DrawRayHitScreenPoint(m_renderer);
	}
	bool PointHandleGizmo::lineEdit(unsigned int id, std::vector<Eigen::Vector3f>& line, Eigen::Vector4<uint8_t> _color)
	{
		FrameParam& cameraParam = m_renderer.getFrameParam();
		if (line.size() < 2)
		{
			drawRayHitScreenPoint();
		}
		if (line.size() > 2)
		{
			return false;
		}

		m_renderer.setAppId(id);
		bool ret = false;

		Eigen::Vector3f planeOrigin(0, 0, 0);
		Eigen::Vector3f planeNormal = -cameraParam.viewDirectioin;
		unsigned int lineId[2] = { m_renderer.makeId("St"), m_renderer.makeId("En") };
		if (line.size() > 0)
		{
			planeOrigin = line.back();
			Eigen::Vector3f oldPoint = line[0];
			if (m_drag.spherePlaneTranslation(m_renderer, lineId[0], line[0], m_renderer.pixelsToWorldSize(line[0], 10), planeNormal,
				cameraParam.snapTranslation, &line[0]))
			{

				if (line.size() >= 2)
				{
					ret = true;
					line[1] = line[1] + line[0] - oldPoint;
				}
			}
		}
		if (line.size() < 2)
		{
			if (m_renderer.wasKeyReleased(ActionSelect) && m_renderer.isKeyDown(ActionControl))
			{
				Ray ray(cameraParam.rayOrigin, cameraParam.rayDirection);
				float t0 = FLT_MAX;
				if (Intersect(ray, Plane(planeNormal, planeOrigin), t0))
				{
					Eigen::Vector3f intersectPoint = ray.m_origin + ray.m_direction * t0;
					line.push_back(intersectPoint);
				}
			}
		}
		for (int i = 0; i < line.size(); i++)
		{

			if (lineId[i] == m_renderer.getHotId())
			{
				m_renderer.pushColor(Color_Gold);
				m_renderer.drawCircle(line[i], planeNormal, m_renderer.pixelsToWorldSize(line[i], 10), 20);
				m_renderer.popColor();
				m_renderer.pushAlpha(0.3);
				m_renderer.pushColor(Color_Red);
				m_renderer.drawCircleFilled(line[i], planeNormal, m_renderer.pixelsToWorldSize(line[i], 10), 20);
				m_renderer.popAlpha();
				m_renderer.popColor();
			}
			m_renderer.drawPoint(line[i], 10, lineId[i] == m_renderer.getHotId() ? Color_Gold : Color_White);
		}
		if (line.size() >= 2)
		{
			m_renderer.pushSize(4.0);
			m_renderer.pushColor(_color);
			m_renderer.drawArrow(line[0], line[1], -1, 15);
			m_renderer.popColor();
			m_renderer.popSize();
			ret |= m_drag.spherePlaneTranslation(m_renderer, lineId[1], line[1], m_renderer.pixelsToWorldSize(line[1], 10), planeNormal, cameraParam.snapTranslation, &line[1]);
		}
		return ret;
	}
	bool PointHandleGizmo::scaleEdit(unsigned int id, std::vector<Eigen::Vector3f>& line, Eigen::Vector4<uint8_t> _color, int& index)
	{
		FrameParam& cameraParam = m_renderer.getFrameParam();
		if (line.size() > 3)
		{
			return false;
		}

		m_renderer.setAppId(id);
		bool ret = false;

		Eigen::Vector3f planeOrigin(0, 0, 0);
		Eigen::Vector3f planeNormal = -cameraParam.viewDirectioin;
		unsigned int lineId[3] = { m_renderer.makeId("Anchor"), m_renderer.makeId("Begin"), m_renderer.makeId("End") };
		if (line.size() > 0)
		{
			planeOrigin = line.back();
			Eigen::Vector3f oldPoint = line[0];
			if (m_drag.spherePlaneTranslation(m_renderer, lineId[0], line[0], m_renderer.pixelsToWorldSize(line[0], 10), planeNormal,
				cameraParam.snapTranslation, &line[0]))
			{
				ret = true;
				index = 0;
				if (line.size() >= 2)
				{
					line[1] = line[1] + line[0] - oldPoint;
				}
				if (line.size() >= 3)
				{
					line[2] = line[2] + line[0] - oldPoint;
				}
			}
		}
		if (line.size() < 3)
		{
			if (m_renderer.wasKeyReleased(ActionSelect) && m_renderer.isKeyDown(ActionControl))
			{
				Ray ray(cameraParam.rayOrigin, cameraParam.rayDirection);
				float t0 = FLT_MAX;
				if (Intersect(ray, Plane(planeNormal, planeOrigin), t0))
				{
					Eigen::Vector3f intersectPoint = ray.m_origin + ray.m_direction * t0;
					line.push_back(intersectPoint);
				}
			}
		}
		for (int i = 0; i < line.size(); i++)
		{

			if (lineId[i] == m_renderer.getHotId())
			{
				m_renderer.pushColor(Color_Gold);
				m_renderer.drawCircle(line[i], planeNormal, m_renderer.pixelsToWorldSize(line[i], 10), 20);
				m_renderer.popColor();
				m_renderer.pushAlpha(0.3);
				m_renderer.pushColor(Color_Red);
				m_renderer.drawCircleFilled(line[i], planeNormal, m_renderer.pixelsToWorldSize(line[i], 10), 20);
				m_renderer.popAlpha();
				m_renderer.popColor();
			}
			m_renderer.drawPoint(line[i], 10, lineId[i] == m_renderer.getHotId() ? Color_Gold : Color_White);
		}
		if (line.size() >= 2)
		{
			m_renderer.drawLine(line[0], line[1], 4, _color);
			Eigen::Vector3f oldPoint = line[1];
			if (m_drag.spherePlaneTranslation(m_renderer, lineId[1], line[1], m_renderer.pixelsToWorldSize(line[1], 10), planeNormal, cameraParam.snapTranslation, &line[1]))
			{
				ret = true;
				index = 1;
				if (line.size() >= 3)
				{
					line[2] = line[2] + line[1] - oldPoint;
				}
			}
		}
		if (line.size() >= 3)
		{
			m_renderer.pushSize(4.0);
			m_renderer.pushColor(_color);
			m_renderer.drawArrow(line[1], line[2], -1, 15);
			m_renderer.popColor();
			m_renderer.popSize();
			if (m_drag.spherePlaneTranslation(m_renderer, lineId[2], line[2], m_renderer.pixelsToWorldSize(line[2], 10), planeNormal, cameraParam.snapTranslation, &line[2]))
			{
				ret = true;
				index = 2;
			}

		}
		return ret;
	}
	bool PointHandleGizmo::planeEdit(unsigned int id, Eigen::Vector3f& _origin, Eigen::Vector3f& _normal)
	{
		FrameParam& cameraParam = m_renderer.getFrameParam();
		m_renderer.setAppId(id);
		bool ret = false;
		Eigen::Vector3f* outNormal = new Eigen::Vector3f(_normal);
		Eigen::Vector3f* outOrigin = new Eigen::Vector3f(_origin);
		Eigen::Vector3f normal = *outNormal;
		Eigen::Vector3f planeOrigin = *outOrigin;

		unsigned int planeArrow = m_renderer.makeId("planeArrow");
		unsigned int planeOriginId = m_renderer.makeId("planeOrigin");
		unsigned int planeOriginCircle = m_renderer.makeId("planeCircle");

		Eigen::Vector3f mouse = cameraParam.rayOrigin + cameraParam.rayDirection;
		
		float worldHeight = m_renderer.pixelsToWorldSize(planeOrigin, 50);
		static float cirleDetectRadius = 5;//
		//pixelsToWorldSize(planeOrigin, 40);


		m_renderer.pushColor(Color_Red);
		m_renderer.popColor();



		ret |= m_drag.spherePlaneTranslation(m_renderer, planeOriginId, planeOrigin, m_renderer.pixelsToWorldSize(planeOrigin, 15), normal, cameraParam.snapTranslation, outOrigin);
		ret |= m_drag.circleAxisTranslation(m_renderer, planeOriginCircle, planeOrigin, cirleDetectRadius, normal, cameraParam.snapTranslation, outOrigin);
		//ret |= gizmoOperateNormalBehavior(planeArrow, planeOrigin, planeOrigin + normal * worldHeight, pixelsToWorldSize(planeOrigin, 3), outNormal);

		planeOrigin = *outOrigin;



		Gizmo::DrawAxisArrow(m_renderer, planeArrow, planeOrigin, normal, worldHeight, Color_Green);
		m_renderer.drawPoint(planeOrigin, 15, planeOriginId == m_renderer.getHotId() ? Color_Gold : Color_White);
		m_renderer.pushAlpha(0.2);
		m_renderer.pushColor(planeOriginCircle == m_renderer.getHotId() ? Color_Green : Color_White);
		m_renderer.pushEnableSorting(true);
		m_renderer.drawCircleFilled(planeOrigin, normal, cirleDetectRadius, 40);
		m_renderer.popEnableSorting();
		m_renderer.pushSize(3.0);
		m_renderer.pushColor(Color_Green);

		m_renderer.drawCircle(planeOrigin, normal, cirleDetectRadius, 40);

		m_renderer.popSize();
		m_renderer.popAlpha();
		m_renderer.popColor();
		m_renderer.popColor();
		Eigen::Vector3f boxMin = Eigen::Vector3f(std::min(-5.0f, planeOrigin.x()), std::min(-5.0f, planeOrigin.y()), std::min(-5.0f, planeOrigin.z()));
		Eigen::Vector3f boxMax = Eigen::Vector3f(std::max(5.0f, planeOrigin.x()), std::max(5.0f, planeOrigin.y()), std::max(5.0f, planeOrigin.z()));

		m_renderer.pushSize(3.0);
		m_renderer.drawAlignedBox(boxMin, boxMax);
		m_renderer.popSize();
		std::vector<Eigen::Vector3f> edges(std::move(clipBox(Plane(normal, planeOrigin), boxMin, boxMax)));
		for (int i = 0; i < edges.size() / 2; i++)
		{
			m_renderer.drawLine(edges[2 * i], edges[2 * i + 1], 4, Color_White);
		}

		Eigen::Vector3f up = abs(normal.y()) > 0.99 ? Eigen::Vector3f(1, 0, 0) : Eigen::Vector3f(0, 1, 0);
		Eigen::Vector3f xaxis = normal.cross(up).normalized();
		Eigen::Vector3f zaxis = xaxis.cross(normal).normalized();
		unsigned int pointId[4]{ m_renderer.makeId("p1"), m_renderer.makeId("p2"), m_renderer.makeId("p3"), m_renderer.makeId("p4") };
		Eigen::Vector3f pointPos[4] = { planeOrigin + xaxis * 1.0 * cirleDetectRadius,planeOrigin - xaxis * 1.0 * cirleDetectRadius, planeOrigin + zaxis * 1.0 * cirleDetectRadius,planeOrigin - zaxis * 1.0 * cirleDetectRadius };
		Eigen::Vector3f pointDir[4] = { xaxis, -xaxis, zaxis, -zaxis };

		for (int i = 0; i < 4; i++)
		{
			m_renderer.drawPoint(pointPos[i], 10, pointId[i] == m_renderer.getHotId() ? Color_Gold : Color_GB);
			Eigen::Vector3f* outFace = new Eigen::Vector3f(0, 0, 0);
			*outFace = pointPos[i];
			float size = m_renderer.pixelsToWorldSize(pointPos[i], 10);
			if (m_drag.sphereAxisTranslation(m_renderer, pointId[i], pointPos[i], size, pointDir[i], cameraParam.snapTranslation, outFace))
			{

				cirleDetectRadius = (*outFace - planeOrigin).norm();

			}
			delete outFace;
		}

		_normal = *outNormal;
		_origin = *outOrigin;
		delete outNormal;
		delete outOrigin;
		return ret;
	}
	bool PointHandleGizmo::planeEdit(unsigned int id, std::vector<Eigen::Vector3f>& threePoints, int& index)
	{
		FrameParam& cameraParam = m_renderer.getFrameParam();
		if (threePoints.size() < 3)
		{
			drawRayHitScreenPoint();
		}
		if (threePoints.size() == 3)
		{

			Eigen::Vector3f ac = threePoints[2] - threePoints[0];
			Eigen::Vector3f ab = threePoints[1] - threePoints[0];
			if (!ac.cross(ab).isApprox(Eigen::Vector3f::Zero()))
			{
				Eigen::Vector3f abXac = ab.cross(ac);
				Eigen::Vector3f abXacXab = abXac.cross(ab);
				Eigen::Vector3f acXabXac = ac.cross(abXac);
				Eigen::Vector3f toCircumsphere =
					(abXacXab * ac.squaredNorm() + acXabXac * ab.squaredNorm()) / (2.0f * abXac.squaredNorm());
				float radius = toCircumsphere.norm();
				Eigen::Vector3f center = threePoints[0] + toCircumsphere;
				m_renderer.pushColor(Color_Gold);
				m_renderer.drawCircle(center, abXac.normalized(), radius, 40);
				m_renderer.popColor();
				m_renderer.pushEnableSorting(true);
				m_renderer.pushAlpha(0.7);
				m_renderer.pushColor(Color_GB);
				m_renderer.drawCircleFilled(center, abXac.normalized(), radius, 40);
				m_renderer.popAlpha();
				m_renderer.popColor();
				m_renderer.popEnableSorting();
			}

		}
		if (threePoints.size() > 3)
		{
			return false;
		}

		m_renderer.setAppId(id);
		bool ret = false;

		Eigen::Vector3f planeOrigin(0, 0, 0);
		Eigen::Vector3f planeNormal = -cameraParam.viewDirectioin;
		unsigned int lineId[3] = { m_renderer.makeId("ThreeA"), m_renderer.makeId("ThreeB"), m_renderer.makeId("ThreeC") };

		if (threePoints.size() < 3)
		{
			if (m_renderer.wasKeyReleased(ActionSelect) && m_renderer.isKeyDown(ActionControl))
			{
				Ray ray(cameraParam.rayOrigin, cameraParam.rayDirection);
				float t0 = FLT_MAX;
				if (Intersect(ray, Plane(planeNormal, planeOrigin), t0))
				{
					Eigen::Vector3f intersectPoint = ray.m_origin + ray.m_direction * t0;
					threePoints.push_back(intersectPoint);
				}
			}
		}
		for (int i = 0; i < threePoints.size(); i++)
		{
			if (m_drag.spherePlaneTranslation(m_renderer, lineId[i], threePoints[i], m_renderer.pixelsToWorldSize(threePoints[i], 10),
				planeNormal, cameraParam.snapTranslation, &threePoints[i]))
			{
				ret = true;
				index = i;
			}


			if (lineId[i] == m_renderer.getHotId())
			{
				m_renderer.pushColor(Color_Gold);
				m_renderer.drawCircle(threePoints[i], planeNormal, m_renderer.pixelsToWorldSize(threePoints[i], 10), 20);
				m_renderer.popColor();
				m_renderer.pushAlpha(0.3);
				m_renderer.pushColor(Color_Red);
				m_renderer.drawCircleFilled(threePoints[i], planeNormal, m_renderer.pixelsToWorldSize(threePoints[i], 10), 20);
				m_renderer.popAlpha();
				m_renderer.popColor();
			}
			m_renderer.drawPoint(threePoints[i], 10, lineId[i] == m_renderer.getHotId() ? Color_Gold : Color_White);
		}

		return ret;
	}
	bool PointHandleGizmo::axisRotateEdit(unsigned int id, std::vector<Eigen::Vector3f>& line, float& angle)
	{
		FrameParam& cameraParam = m_renderer.getFrameParam();
		// appId = id;  
		// pushEnableSorting(true);
		if (line.size() < 2)
		{
			drawRayHitScreenPoint();
		}
		bool ret = false;
		if (line.size() >= 2)
		{
			unsigned int yAxiasId = m_renderer.makeId("yAxias");

			Gizmo::DrawRotationRing(m_renderer, yAxiasId, line[0], (line[1] - line[0]).normalized(), m_renderer.pixelsToWorldSize(line[0], 80), Color_White);
			ret |= m_drag.axisAngle(m_renderer, yAxiasId, line[0], (line[1] - line[0]).normalized(), cameraParam.snapRotation, m_renderer.pixelsToWorldSize(line[0], 80), m_renderer.pixelsToWorldSize(line[0], Gizmo::kHandleLinePixels), &angle);

		}
		m_renderer.pushId(id);
		ret |= lineEdit(id, line, Color_ARB);
		m_renderer.popId();
		//popEnableSorting();
		return ret;
	}
	bool PointHandleGizmo::dragPointInPlane(unsigned int id, Eigen::Vector3f& translation, const Eigen::Vector3f& normal)
	{
		FrameParam& cameraParam = m_renderer.getFrameParam();
		bool ret = false;
		Eigen::Vector3f* outVec3 = new Eigen::Vector3f(translation);

		Eigen::Vector3f drawAt = *outVec3;
		m_renderer.pushId(id);
		m_renderer.setAppId(id);

		ret |= m_drag.spherePlaneTranslation(m_renderer, id, drawAt, m_renderer.pixelsToWorldSize(drawAt, 15), normal, cameraParam.snapTranslation, outVec3);
		m_renderer.drawPoint(drawAt, 10);

		m_renderer.popId();
		translation = *outVec3;
		delete outVec3;
		return ret;
	}
	bool PointHandleGizmo::dragPointByAxis(unsigned int id, Eigen::Vector3f& translation, const Eigen::Vector3f& axis)
	{
		FrameParam& cameraParam = m_renderer.getFrameParam();
		bool ret = false;
		Eigen::Vector3f* outVec3 = new Eigen::Vector3f(translation);

		Eigen::Vector3f drawAt = *outVec3;
		m_renderer.pushId(id);
		m_renderer.setAppId(id);
		ret |= m_drag.sphereAxisTranslation(m_renderer, id, translation, m_renderer.pixelsToWorldSize(drawAt, 15), axis, cameraParam.snapTranslation, outVec3);

		m_renderer.drawPoint(drawAt, 10);

		m_renderer.popId();
		translation = *outVec3;
		delete outVec3;
		return ret;
	}
	bool PointHandleGizmo::translate2D(unsigned int id, Eigen::Vector2f& pos)
	{
		FrameParam& cameraParam = m_renderer.getFrameParam();
		float radius = 10.0f;

		bool ret = false;
		bool hitFlag = (pos - cameraParam.cursor).norm() < radius;

		if (id == m_renderer.getActiveId2D())
		{
			if (m_renderer.isKeyDown(ActionSelect))
			{
				pos = cameraParam.cursor + m_drag.offset();
				ret = true;
			}
			else
			{
				m_renderer.makeActive2D(IdInvalid);
			}
		}
		else if (id == m_renderer.getHotId2D())
		{
			if (hitFlag)
			{
				if (m_renderer.isKeyDown(ActionSelect))
				{
					m_renderer.makeActive2D(id);
					m_drag.offset() = pos - cameraParam.cursor;
				}
			}
			else
			{
				m_renderer.resetId2D();
			}
		}
		else
		{
			if (hitFlag)
			{
				m_renderer.makeHot2D(id);
			}
		}
		pos.x() = Clamp<float>(pos.x(), 0.0f, cameraParam.viewportWidth);
		pos.y() = Clamp<float>(pos.y(), 0.0f, cameraParam.viewportHeight);
		return ret;
	}
	bool PointHandleGizmo::lineSplit(unsigned int id, Eigen::Vector2f& a, Eigen::Vector2f& b)
	{
		m_renderer.pushId(id);
	    static ImU32 c1 = ImGui::ColorConvertFloat4ToU32({ 1, 1, 0, 1 });
		static ImU32 c2 = ImGui::ColorConvertFloat4ToU32({ 0.0f, 0.9215f, 1.0f, 1.0f });
		static unsigned int sid = m_renderer.makeId("Start");
		static unsigned int eid = m_renderer.makeId("End");
		static unsigned int mid = m_renderer.makeId("Mid");
		Eigen::Vector2f m = (a + b) / 2;
		Eigen::Vector2f delta = m - a;
	    Rendering::Overlay::Im2D::DrawList& drawList = Rendering::Overlay::Im2DRender::Instance().GetDrawList();
		drawList.AddCircleFilled(Rendering::Overlay::Im2D::Vec2(a.x(), a.y()), 8.0f, m_renderer.isHot2D(sid) ? c1 : c2);
		drawList.AddCircleFilled(Rendering::Overlay::Im2D::Vec2(b.x(), b.y()), 8.0f, m_renderer.isHot2D(eid) ? c1 : c2);
		drawList.AddCircleFilled(Rendering::Overlay::Im2D::Vec2(m.x(), m.y()), 8.0f, m_renderer.isHot2D(mid) ? c1 : c2);
		drawList.AddLine(Rendering::Overlay::Im2D::Vec2(a.x(), a.y()), Rendering::Overlay::Im2D::Vec2(b.x(), b.y()),
			Rendering::Overlay::Im2D::ColorU32(255, 255, 0, 255));
		bool ret = false;
		ret |= translate2D(sid,a);
		ret |= translate2D(eid, b);
		if(translate2D(mid, m)){
			ret = true;
			a = m - delta;
			b = m + delta;
		}
		
		m_renderer.popId();
		return ret;
	}
}
