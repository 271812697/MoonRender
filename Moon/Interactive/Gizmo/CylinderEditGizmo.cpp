#include "Interactive/Gizmo/CylinderEditGizmo.h"

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
	CylinderEditGizmo::CylinderEditGizmo(ImRenderer& p_renderer) : m_renderer(p_renderer) { }

	bool CylinderEditGizmo::edit(unsigned int _id, Eigen::Vector3f& _translation_, Eigen::Vector3f& _normal, float& height, float& _tr, float& _br, bool same)
	{
		FrameParam& cameraParam = m_renderer.getFrameParam();
		if (same)
		{
			_br = _tr;
		}


		bool ret = false;
		unsigned int centerId = m_renderer.makeId("Clydiner");
		//ret |= translation(_id, _translation_, false);

		Eigen::Vector3f* outVec3 = new Eigen::Vector3f(_translation_);
		Eigen::Vector3f* outNormal = new Eigen::Vector3f(_normal);
		Eigen::Vector3f drawAt = *outVec3;
		Eigen::Vector3f normal = *outNormal;
		ret |= m_drag.spherePlaneTranslation(m_renderer, centerId, _translation_, m_renderer.pixelsToWorldSize(_translation_, 15), _normal, cameraParam.snapTranslation, outVec3);

		//
		float worldHeight = m_renderer.pixelsToWorldSize(drawAt, 40);
		float worldSize = m_renderer.pixelsToWorldSize(drawAt, 5);
		m_renderer.pushId(_id);
		
		m_renderer.setAppId(_id);
		unsigned int topArrow = m_renderer.makeId("topArrow");
		unsigned int bottomArrow = m_renderer.makeId("bottomArrow");
		unsigned int opNormal = m_renderer.makeId("makeNormal");
		Eigen::Vector3f up = abs(normal.y()) > 0.99 ? Eigen::Vector3f(1, 0, 0) : Eigen::Vector3f(0, 1, 0);
		Eigen::Vector3f xaxis = normal.cross(up).normalized();

		Eigen::Vector3f zaxis = xaxis.cross(normal).normalized();
		Eigen::Vector3f axis[6] = { xaxis, -xaxis, zaxis, -zaxis, normal, -normal };
		float h = height / 2;
		float tr = _tr;
		float br = _br;
		struct PointCenter
		{
			unsigned int centerId;
			Eigen::Vector3f pos;
			Eigen::Vector3f nor;
			float size;
			// float* outS;
			PointCenter(const unsigned int& c, const  Eigen::Vector3f& v, const  Eigen::Vector3f& n)
				: centerId(c)
				, pos(v)
				, nor(n)
			{
			}
		};
		PointCenter topCircle[4] = { {m_renderer.makeId("TPX"), drawAt + normal * h + xaxis * tr, axis[0]},
			{m_renderer.makeId("TNX"), drawAt + normal * h - xaxis * tr, axis[1]},
			{m_renderer.makeId("TPZ"), drawAt + normal * h + zaxis * tr, axis[2]},
			{m_renderer.makeId("TNZ"), drawAt + normal * h - zaxis * tr, axis[3]} };
		PointCenter bottomCircle[4] = { {m_renderer.makeId("BPX"), drawAt - normal * h + xaxis * br, axis[0]},
			{m_renderer.makeId("BNX"), drawAt - normal * h - xaxis * br, axis[1]},
			{m_renderer.makeId("BPZ"), drawAt - normal * h + zaxis * br, axis[2]},
			{m_renderer.makeId("BNZ"), drawAt - normal * h - zaxis * br, axis[3]} };
		for (int i = 0; i < 4; i++)
		{
			Eigen::Vector3f* outFace = new  Eigen::Vector3f(0, 0, 0);
			*outFace = topCircle[i].pos;
			float size = m_renderer.pixelsToWorldSize(topCircle[i].pos, 15);
			if (m_drag.circleAxisTranslation(m_renderer, topCircle[i].centerId, topCircle[i].pos, size, topCircle[i].nor,
				cameraParam.snapTranslation, outFace))
			{
				ret = true;
				_tr = (*outFace - drawAt - normal * h).norm();
				if (same)
				{
					_br = _tr;
				}
			}
			delete outFace;
		}
		for (int i = 0; i < 4; i++)
		{
			Eigen::Vector3f* outFace = new  Eigen::Vector3f(0, 0, 0);
			*outFace = bottomCircle[i].pos;
			float size = m_renderer.pixelsToWorldSize(bottomCircle[i].pos, 15);
			if (m_drag.circleAxisTranslation(m_renderer, bottomCircle[i].centerId, bottomCircle[i].pos, size, bottomCircle[i].nor,
				cameraParam.snapTranslation, outFace))
			{
				ret = true;
				_br = (*outFace - drawAt + normal * h).norm();
				if (same)
				{
					_tr = _br;
				}
			}
			delete outFace;
		}
		Eigen::Vector3f* outFace = new  Eigen::Vector3f(drawAt + normal * h);
		if (m_drag.axisTranslation(m_renderer, 
			topArrow, drawAt + normal * h, normal, cameraParam.snapTranslation, worldHeight, worldSize, outFace))
		{
			ret = true;
			float len = (*outFace - drawAt).norm();
			height = (height / 2 + len);
			*outVec3 = *outVec3 + (*outFace - drawAt - normal * h) / 2;
		}
		else
		{
			*outFace = drawAt - normal * h;
			if (m_drag.axisTranslation(m_renderer, bottomArrow, drawAt - normal * h, -normal, cameraParam.snapTranslation,
				worldHeight, worldSize, outFace))
			{
				ret = true;
				float len = (*outFace - drawAt).norm();
				height = (height / 2 + len);
				*outVec3 = *outVec3 + (*outFace - drawAt + normal * h) / 2;
			}

		}

		// Operate normal

		float sphereRadius = m_renderer.pixelsToWorldSize(drawAt + normal * h, 10.0);
		Ray ray(cameraParam.rayOrigin, cameraParam.rayDirection);
		Sphere sphere(drawAt + normal * h, sphereRadius);
		float t0 = FLT_MAX, t1 = FLT_MAX;
		bool intersects = Intersect(ray, sphere, t0, t1);
		intersects |= Intersect(ray, Sphere(drawAt - normal * h, sphereRadius), t0, t1);
		Eigen::Vector2f& storeCursor = m_drag.cursor();
		Eigen::Vector3f& storeNormal = m_drag.normal();

		if (opNormal == m_renderer.getActiveId())
		{
			if (m_renderer.isKeyDown(ActionSelect))
			{
				Eigen::Vector2<float> offset = 0.5 * (cameraParam.cursor - storeCursor) / 180.0 * Pi;
				offset.y() *= -1;
				Eigen::Matrix3f view = cameraParam.view.block(0, 0, 3, 3);
				Eigen::Vector3f axis = (view.row(1) * offset.x() - view.row(0) * offset.y()).normalized();
				Eigen::Matrix3f mat = RotationMatrix(axis, offset.norm());
				*outNormal = (mat * storeNormal).normalized();
				ret = true;
			}
			else
			{
				m_renderer.makeActive(IdInvalid);
			}
		}
		else if (opNormal == m_renderer.getHotId())
		{
			if (intersects)
			{
				if (m_renderer.isKeyDown(ActionSelect))
				{

					storeCursor = cameraParam.cursor;

					storeNormal = normal.normalized();

					m_renderer.makeActive(opNormal);
				}
			}
			else
			{
				m_renderer.resetId();
			}
		}
		else
		{

			if (intersects)
			{
				m_renderer.makeHot(opNormal, t0, intersects);
			}
		}

		normal = *outNormal;
		m_renderer.drawPoint(drawAt, 10, centerId == m_renderer.getHotId() ? Color_Gold : Color_White);
		m_renderer.drawCircle(drawAt - normal * h, normal, br, 30);
		m_renderer.drawCircle(drawAt + normal * h, normal, tr, 30);
		m_renderer.drawLine(drawAt + normal * h + xaxis * tr, drawAt - normal * h + xaxis * br, 2, Color_Green);
		m_renderer.drawLine(drawAt + normal * h - xaxis * tr, drawAt - normal * h - xaxis * br, 2, Color_Green);
		m_renderer.drawLine(drawAt + normal * h + zaxis * tr, drawAt - normal * h + zaxis * br, 2, Color_Green);
		m_renderer.drawLine(drawAt + normal * h - zaxis * tr, drawAt - normal * h - zaxis * br, 2, Color_Green);
		


		for (int i = 0;i < 4;i++) {
			m_renderer.drawPoint(topCircle[i].pos, 8, topCircle[i].centerId==m_renderer.getHotId()? Color_Gold : Color_Green);
			m_renderer.drawPoint(bottomCircle[i].pos, 8, bottomCircle[i].centerId == m_renderer.getHotId() ? Color_Gold : Color_Green);
		}
		m_renderer.drawPoint(drawAt + normal * (h), 8, opNormal == m_renderer.getHotId() ? Color_Gold : Color_Green);
		m_renderer.drawPoint(drawAt - normal * (h), 8, opNormal == m_renderer.getHotId() ? Color_Gold : Color_Green);
		Gizmo::DrawAxisArrow(m_renderer, topArrow, drawAt + normal * (height / 2), normal, worldHeight, Color_Green);
		Gizmo::DrawAxisArrow(m_renderer, bottomArrow, drawAt - normal * (height / 2), -normal, worldHeight, Color_Green);

		m_renderer.popId();


		static bool me = false;
		static Eigen::Vector3f meToPoint;
		if (m_renderer.wasKeyReleased(ActionSelect) && m_renderer.isKeyDown(ActionControl))
		{

			float p[3] = { 0, 0, 0 };
			if (false)
			{
				me = true;
				meToPoint = { p[0], p[1], p[2] };
			}

		}
		if (me)
		{
			*outVec3 = 0.85 * (*outVec3) + 0.15 * meToPoint;
			if ((*outVec3 - meToPoint).norm() < 0.03f)
			{
				*outVec3 = meToPoint;
				me = false;

			}
			ret = true;
		}
		_translation_ = *outVec3;
		_normal = *outNormal;
		delete outVec3;
		delete outNormal;
		return ret;
	}
}
