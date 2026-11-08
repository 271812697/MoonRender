#include "Interactive/Gizmo/SphereEditGizmo.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <vector>
#include <Eigen/Core>

#include "Interactive/Im3DRenderer.h"
#include "Interactive/Im3DPalette.h"
#include "Interactive/MathUtil/MathUtil.h"

namespace MOON
{
	SphereEditGizmo::SphereEditGizmo(ImRenderer& p_renderer) : m_renderer(p_renderer) { }

	bool SphereEditGizmo::edit(unsigned int _id, Eigen::Vector3f& _translation_, float& r)
	{
		FrameParam& cameraParam = m_renderer.getFrameParam();
		bool ret = false;
		Eigen::Vector3f* outVec3 = new Eigen::Vector3f(0, 0, 0);
		*outVec3 = _translation_;
		Eigen::Vector3f drawAt = *outVec3;
		m_renderer.pushId(_id);
		m_renderer.setAppId(_id);
		unsigned int centerId = m_renderer.makeId("Sphere");
		ret |= m_drag.spherePlaneTranslation(m_renderer, centerId, drawAt, m_renderer.pixelsToWorldSize(drawAt, 15),
			cameraParam.viewDirectioin, cameraParam.snapTranslation, outVec3);
		Eigen::Vector3f axis[6] = { Eigen::Vector3f(1, 0, 0), Eigen::Vector3f(-1, 0, 0), Eigen::Vector3f(0, 1, 0),
			Eigen::Vector3f(0, -1, 0), Eigen::Vector3f(0, 0, 1), Eigen::Vector3f(0, 0, -1) };

		struct PointCenter
		{
			unsigned int centerId;
			Eigen::Vector3f pos;
			Eigen::Vector3f nor;
			float size;
			// float* outS;
			PointCenter(const unsigned int& c, const Eigen::Vector3f& v, const Eigen::Vector3f& n)
				: centerId(c)
				, pos(v)
				, nor(n)
			{
			}
		};
		PointCenter points[6] = {
			{m_renderer.makeId("RightPointCenter"), Eigen::Vector3f(drawAt + axis[0] * r), axis[0]},
			{m_renderer.makeId("LeftPointCenter"), Eigen::Vector3f(drawAt + axis[1] * r), axis[1]},
			{m_renderer.makeId("TopPointCenter"), Eigen::Vector3f(drawAt + axis[2] * r), axis[2]},
			{m_renderer.makeId("BottomPointCenter"), Eigen::Vector3f(drawAt + axis[3] * r), axis[3]},
			{m_renderer.makeId("FrontPointCenter"), Eigen::Vector3f(drawAt + axis[4] * r), axis[4]},
			{m_renderer.makeId("BackPointCenter"), Eigen::Vector3f(drawAt + axis[5] * r), axis[5]},
		};

		for (int i = 0; i < 6; i++)
		{
			Eigen::Vector3f* outFace = new Eigen::Vector3f(0, 0, 0);
			*outFace = points[i].pos;
			float size = m_renderer.pixelsToWorldSize(points[i].pos, 25);
			if (m_drag.circleAxisTranslation(m_renderer, 
				points[i].centerId, points[i].pos, size, points[i].nor, cameraParam.snapTranslation, outFace))
			{

				*outVec3 = *outVec3 + (*outFace - points[i].pos) / 2;
				r = (*outFace - *outVec3).norm();
				ret = true;
			}
			delete outFace;
		}
		m_renderer.pushSize(3.0);
		m_renderer.drawPoint(drawAt, 10, centerId == m_renderer.getHotId() ? Color_Gold : Color_White);
		m_renderer.drawSphere(drawAt, r, 40);
		for (int i = 0; i < 6; i++)
		{
			m_renderer.drawPoint(drawAt + axis[i] * r, 8, points[i].centerId == m_renderer.getHotId() ? Color_Gold : Color_Green);
		}


		m_renderer.popSize();
		m_renderer.popId();
		_translation_ = *outVec3;
		delete outVec3;
		return ret;
	}
}
