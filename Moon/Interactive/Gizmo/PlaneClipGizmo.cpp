#include "Interactive/Gizmo/PlaneClipGizmo.h"

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
	PlaneClipGizmo::PlaneClipGizmo(ImRenderer& p_renderer) : m_renderer(p_renderer) { }

	bool PlaneClipGizmo::edit(unsigned int _id, Eigen::Vector3f& _translation_, Eigen::Vector3f& _normal, float& _scale, const Eigen::Vector3f& minBox, const Eigen::Vector3f& maxBox, bool opeartorNormal, bool drawCircleFlag)
	{
		FrameParam& cameraParam = m_renderer.getFrameParam();
		bool ret = false;
		Eigen::Vector3f* outVec3 = new Eigen::Vector3f(0, 0, 0);
		*outVec3 = _translation_;
		Eigen::Vector3f* outNormal = new Eigen::Vector3f(_normal);
		Eigen::Vector3f drawAt = *outVec3;
		Eigen::Vector3f normal = *outNormal;
		// idStack.push_back(_id);
		m_renderer.setAppId(_id);
		unsigned int sphereId = m_renderer.makeId("Sphere");
		unsigned int circleId = m_renderer.makeId("Circle");
		unsigned int arrowId = m_renderer.makeId("Arrow");


		Eigen::Vector3f up = abs(normal.y()) > 0.99 ? Eigen::Vector3f(1, 0, 0) : Eigen::Vector3f(0, 1, 0);
		Eigen::Vector3f xaxis = normal.cross(up).normalized();
		Eigen::Vector3f zaxis = xaxis.cross(normal).normalized();
		unsigned int pointId[4]{ m_renderer.makeId("p1"), m_renderer.makeId("p2"), m_renderer.makeId("p3"), m_renderer.makeId("p4") };
		Eigen::Vector3f pointPos[4] = {
			drawAt + xaxis * 1.0 * _scale, drawAt - xaxis * 1.0 * _scale,
			drawAt + zaxis * 1.0 * _scale, drawAt - zaxis * 1.0 * _scale };
		Eigen::Vector3f pointDir[4] = { xaxis,-xaxis,zaxis,-zaxis };
		float worldSize = m_renderer.pixelsToWorldSize(drawAt, 3.0);

		// 0.1 / scale;
		// draw spheres
		//drawOneMesh(drawAt, { 0.03f * _scale, 0.03f * _scale, 0.03f * _scale },
		//    sphereId == hotId ? Eigen::Vector3f(0, 1, 0) : Eigen::Vector3f(1, 0, 0), "Sphere");

		auto rotation = RotationMatrixX(normal.normalized());
		Eigen::Vector3f scale = { 0.025f * _scale, 0.025f * _scale, 0.025f * _scale };
		Eigen::Vector3f translate1 = drawAt + normal * _scale;
		Eigen::Vector3f translate2 = drawAt - normal * _scale;
		//drawOneMesh(translate1, rotation, scale, arrowId == hotId ? Eigen::Vector3f(0, 1, 0) : Eigen::Vector3f(1, 0, 0),
		//    "ClipCone");
		//drawOneMesh(translate2, rotation, scale, arrowId == hotId ? Eigen::Vector3f(0, 1, 0) : Eigen::Vector3f(1, 0, 0),
		//    "ClipCone");

		ret |= m_drag.spherePlaneTranslation(m_renderer, sphereId, drawAt, 0.05 * _scale,
			normal, cameraParam.snapTranslation, outVec3);
		ret |= m_drag.circleAxisTranslation(m_renderer, 
			circleId, drawAt, 1.0 * _scale, normal, cameraParam.snapTranslation, outVec3);

		// Operate normal
		/***/
		if (opeartorNormal)
		{
			Ray ray(cameraParam.rayOrigin, cameraParam.rayDirection);

			Capsule axisCapsule(drawAt - normal * _scale, drawAt + normal * _scale, worldSize);

			float t0 = FLT_MAX, t1 = FLT_MAX, t2 = FLT_MAX, t3 = FLT_MAX;
			bool intersects = Intersect(ray, axisCapsule, t0, t1); //
			intersects |= IntersectWithCone(ray, drawAt + normal * _scale, normal, 0.173 * _scale, 0.1 * _scale, t2);
			intersects |= IntersectWithCone(ray, drawAt + normal * (-1.173 * _scale), normal, 0.173 * _scale, 0.1 * _scale, t3);
			Eigen::Vector2<float>& storeCursor = m_drag.cursor();
			Eigen::Vector3f& storeNormal = m_drag.normal();

			if (arrowId == m_renderer.getActiveId())
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
			else if (arrowId == m_renderer.getHotId())
			{
				if (intersects)
				{
					if (m_renderer.isKeyDown(ActionSelect))
					{

						storeCursor = cameraParam.cursor;
						storeNormal = normal.normalized();
						m_renderer.makeActive(arrowId);
					}
				}
				else
				{
					m_renderer.resetId();
				}
			}
			else
			{
				float tr = std::fmin(t0, t1);
				tr = std::fmin(tr, t2);
				tr = std::fmin(tr, t3);
				if (intersects)
				{
					m_renderer.makeHot(arrowId, tr, intersects);
				}
			}
		}



		/***/
		if (arrowId == m_renderer.getHotId() || circleId == m_renderer.getHotId())
		{
			m_renderer.pushColor(Eigen::Vector4<uint8_t>(255, 0, 255, 0));
		}
		else
		{
			m_renderer.pushColor(Eigen::Vector4<uint8_t>(255, 0, 0, 255));
		}

		m_renderer.pushSize(4.0);
		m_renderer.pushEnableSorting(true);
		m_renderer.drawLine(drawAt - normal * _scale, drawAt + normal * _scale, 2.5);
		m_renderer.drawCircle(drawAt, normal, 1.0 * _scale, 40);

		m_renderer.popEnableSorting();

		for (int i = 0; i < 4; i++)
		{
			m_renderer.drawPoint(pointPos[i], 8, pointId[i] == m_renderer.getHotId() ? Color_Gold : Color_GB);
			Eigen::Vector3f* outFace = new Eigen::Vector3f(0, 0, 0);
			*outFace = pointPos[i];
			float size = m_renderer.pixelsToWorldSize(pointPos[i], 15);
			if (m_drag.sphereAxisTranslation(m_renderer, pointId[i], pointPos[i], size, pointDir[i], cameraParam.snapTranslation, outFace))
			{

				_scale = (*outFace - drawAt).norm();

			}
			delete outFace;
		}
		m_renderer.pushEnableSorting(true);
		m_renderer.popSize();
		m_renderer.popColor();
		m_renderer.pushSize(1.50);
		Eigen::Vector3f lb = Eigen::Vector3f(std::min(_translation_.x(), minBox.x()), std::min(_translation_.y(), minBox.y()), std::min(_translation_.z(), minBox.z()));
		Eigen::Vector3f rt = Eigen::Vector3f(std::max(_translation_.x(), maxBox.x()), std::max(_translation_.y(), maxBox.y()), std::max(_translation_.z(), maxBox.z()));
		m_renderer.drawAlignedBox(lb, rt);
		m_renderer.popSize();
		std::vector<Eigen::Vector3f> edges(std::move(clipBox(Plane(normal, drawAt), lb, rt)));

		for (int i = 0; i < edges.size() / 2; i++)
		{

			m_renderer.drawLine(edges[2 * i], edges[2 * i + 1], 2.0, circleId == m_renderer.getHotId() ? Color_Green : Color_White);
		}

		m_renderer.pushAlpha(0.3);
		if (circleId == m_renderer.getHotId())
		{
			m_renderer.pushColor(Eigen::Vector4<uint8_t>(255, 0, 255, 0));
		}
		else
		{
			m_renderer.pushColor(Eigen::Vector4<uint8_t>(255, 255, 255, 255));
		}
		if (drawCircleFlag)
		{
			m_renderer.drawCircleFilled(drawAt, normal, 1.0 * _scale, 40);
		}


		m_renderer.popColor();
		m_renderer.popAlpha();
		m_renderer.popEnableSorting();
		// idStack.pop_back();
		_normal = *outNormal;
		_translation_ = *outVec3;
		delete outNormal;
		delete outVec3;
		return ret;
	}
}
