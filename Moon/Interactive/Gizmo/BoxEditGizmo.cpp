#include "Interactive/Gizmo/BoxEditGizmo.h"

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
	BoxEditGizmo::BoxEditGizmo(ImRenderer& p_renderer) : m_renderer(p_renderer) { }

	bool BoxEditGizmo::edit(unsigned int _id, Eigen::Vector3f& _translation_, Eigen::Matrix3f& _rotation_, Eigen::Vector3f& _scale_)
	{
		FrameParam& cameraParam = m_renderer.getFrameParam();
		bool ret = false;
		Eigen::Vector3f* outVec3 = new Eigen::Vector3f(0, 0, 0);
		*outVec3 = _translation_;
		Eigen::Vector3f drawAt = *outVec3;
		Eigen::Matrix3f* outMat3 = new Eigen::Matrix3f(_rotation_);
		Eigen::Matrix3f drawMat = *outMat3;
		Eigen::Vector3f* outScale = new Eigen::Vector3f(_scale_);
		Eigen::Vector3f drawScale = *outScale;

		Eigen::Vector3f xAxis((*outMat3)(0, 0), (*outMat3)(1, 0), (*outMat3)(2, 0));
		Eigen::Vector3f yAxis((*outMat3)(0, 1), (*outMat3)(1, 1), (*outMat3)(2, 1));
		Eigen::Vector3f zAxis((*outMat3)(0, 2), (*outMat3)(1, 2), (*outMat3)(2, 2));
		float worldHeight = m_renderer.pixelsToWorldSize(drawAt, 10);
		m_renderer.pushId(_id);
		m_renderer.setAppId(_id);
		unsigned int boxCenter = m_renderer.makeId("BoxCenter");
		unsigned int boundingBox = m_renderer.makeId("BoundingBox");
		bool canRotateBox = true;
		Ray camRay(cameraParam.rayOrigin, cameraParam.rayDirection);
		if (boundingBox != m_renderer.getActiveId() && Intersects(camRay, Sphere(drawAt, m_renderer.pixelsToWorldSize(drawAt, 15))))
		{
			canRotateBox = false;
		}
		Eigen::Vector3f viewNormal = cameraParam.viewDirectioin;
		if (m_drag.planeTranslation(m_renderer, boxCenter, drawAt, viewNormal, cameraParam.snapTranslation, worldHeight, outVec3))
		{
			ret = true;
		}
		struct FaceCenter
		{
			unsigned int centerId;
			Eigen::Vector3f pos;
			Eigen::Vector3f nor;
			float size;
			float* outS;
			FaceCenter(const unsigned int& c, const Eigen::Vector3f& v, const Eigen::Vector3f& n, float* s)
				: centerId(c)
				, pos(v)
				, nor(n)
				, outS(s)
			{
			}
		};
		FaceCenter faces[6] = {
			{m_renderer.makeId("RightFaceCenter"), Eigen::Vector3f(drawAt + xAxis * drawScale.x()), xAxis, &(outScale->x())},
			{m_renderer.makeId("LeftFaceCenter"), Eigen::Vector3f(drawAt - xAxis * drawScale.x()), xAxis, &(outScale->x())},
			{m_renderer.makeId("TopFaceCenter"), Eigen::Vector3f(drawAt + yAxis * drawScale.y()), yAxis, &(outScale->y())},
			{m_renderer.makeId("BottomFaceCenter"), Eigen::Vector3f(drawAt - yAxis * drawScale.y()), yAxis, &(outScale->y())},
			{m_renderer.makeId("FrontCenter"), Eigen::Vector3f(drawAt + zAxis * drawScale.z()), zAxis, &(outScale->z())},
			{m_renderer.makeId("BackFaceCenter"), Eigen::Vector3f(drawAt - zAxis * drawScale.z()), zAxis, &(outScale->z())},
		};

		for (int i = 0; i < 6; i++)
		{
			Eigen::Vector3f* outFace = new Eigen::Vector3f(0, 0, 0);
			*outFace = faces[i].pos;
			float size = m_renderer.pixelsToWorldSize(faces[i].pos, 10);
			if (m_drag.sphereAxisTranslation(m_renderer, faces[i].centerId, faces[i].pos, size, faces[i].nor, cameraParam.snapTranslation, outFace))
			{
				ret = true;
				*outVec3 = *outVec3 + (*outFace - faces[i].pos) / 2;
				*faces[i].outS = (*outFace - *outVec3).norm();
			}
			if (boundingBox != m_renderer.getActiveId() && Intersects(camRay, Sphere(faces[i].pos, size)))
			{
				canRotateBox = false;
			}
			delete outFace;
		}

		if (canRotateBox)
		{
			Eigen::Matrix4f mat = Coord3(drawAt, drawMat, drawScale);
			Eigen::Matrix4f matinverse = mat.inverse();
			Eigen::Vector3f o = MatrixMulPoint(matinverse, cameraParam.rayOrigin);
			Eigen::Vector4<float> temp = matinverse * Eigen::Vector4<float>(cameraParam.rayDirection.x(), cameraParam.rayDirection.y(), cameraParam.rayDirection.z(), 0.0);
			Eigen::Vector3f d = temp.head<3>().normalized();
			Ray ray(o, d);
			float tr = FLT_MAX;
			bool hit = IntersectBox(ray, Eigen::Vector3f(-1, -1, -1), Eigen::Vector3f(1, 1, 1), tr);
			if (hit)
			{

				tr = (MatrixMulPoint(mat, ray.m_origin + ray.m_direction * tr) - cameraParam.rayOrigin).norm();
			}

			Eigen::Vector2<float>& storeCursor = m_drag.cursor();
			Eigen::Matrix3f& storeMat = m_drag.basis();

			if (boundingBox == m_renderer.getActiveId())
			{
				if (m_renderer.isKeyDown(ActionSelect))
				{
					Eigen::Vector2<float> offset;
					offset = 0.5 * (cameraParam.cursor - storeCursor) / 180.0 * Pi;
					offset.y() *= -1;

					Eigen::Matrix3f view = cameraParam.view.block(0, 0, 3, 3);
					Eigen::Vector3f axis = (view.row(1) * offset.x() - view.row(0) * offset.y()).normalized();

					Eigen::Vector2<float> debug = offset.normalized();

					Eigen::Vector3f s = drawAt + Eigen::Vector3f(100 * (view.row(0) * debug.y() - view.row(1) * debug.x()));
					Eigen::Vector3f e = drawAt + Eigen::Vector3f(100 * (-view.row(0) * debug.y() + view.row(1) * debug.x()));
					m_renderer.drawLine(s, e, 3, Color_Gold);
					*outMat3 = RotationMatrix(axis, offset.norm()) * storeMat;
					ret = true;
				}
				else
				{
					m_renderer.makeActive(IdInvalid);
				}
			}
			else if (boundingBox == m_renderer.getHotId())
			{
				if (hit)
				{
					if (m_renderer.isKeyDown(ActionSelect))
					{
						storeCursor = cameraParam.cursor;
						storeMat = *outMat3;
						m_renderer.makeActive(boundingBox);
					}
					m_renderer.updateDepth(tr);
				}
				else
				{
					m_renderer.resetId();
				}
			}
			else
			{
				m_renderer.makeHot(boundingBox, tr, hit);
			}
		}
		else if (boundingBox == m_renderer.getHotId())
		{
			m_renderer.resetId();
		}
		_translation_ = *outVec3;
		_rotation_ = *outMat3;
		_scale_ = *outScale;
		delete outVec3;
		delete outMat3;
		delete outScale;
		m_renderer.pushMatrix(Coord3(_translation_, _rotation_, _scale_));

		struct Quad
		{
			Eigen::Vector3f a;
			Eigen::Vector3f b;
			Eigen::Vector3f c;
			Eigen::Vector3f d;
		};

		Eigen::Vector3f points[6] = { Eigen::Vector3f(1, 0, 0), Eigen::Vector3f(-1, 0, 0),
			Eigen::Vector3f(0, 1, 0), Eigen::Vector3f(0, -1, 0), Eigen::Vector3f(0, 0, 1),
			Eigen::Vector3f(0, 0, -1) };
		Quad quads[6] = { {Eigen::Vector3f(1, -1, -1), Eigen::Vector3f(1, 1, -1), Eigen::Vector3f(1, 1, 1),
							 Eigen::Vector3f(1, -1, 1)},
			{Eigen::Vector3f(-1, -1, -1), Eigen::Vector3f(-1, 1, -1), Eigen::Vector3f(-1, 1, 1),
				Eigen::Vector3f(-1, -1, 1)},
			{Eigen::Vector3f(-1, 1, -1), Eigen::Vector3f(1, 1, -1), Eigen::Vector3f(1, 1, 1),
				Eigen::Vector3f(-1, 1, 1)},
			{Eigen::Vector3f(-1, -1, -1), Eigen::Vector3f(1, -1, -1), Eigen::Vector3f(1, -1, 1),
				Eigen::Vector3f(-1, -1, 1)},
			{Eigen::Vector3f(-1, -1, 1), Eigen::Vector3f(1, -1, 1), Eigen::Vector3f(1, 1, 1),
				Eigen::Vector3f(-1, 1, 1)},
			{Eigen::Vector3f(-1, -1, -1), Eigen::Vector3f(1, -1, -1), Eigen::Vector3f(1, 1, -1),
				Eigen::Vector3f(-1, 1, -1)}

		};

		for (int i = 0; i < 6; i++)
		{
			m_renderer.drawPoint(points[i], 7, faces[i].centerId == m_renderer.getHotId() ? Color_Gold : Color_Green);
			if (faces[i].centerId == m_renderer.getHotId())
			{
				m_renderer.pushAlpha(0.3);
				m_renderer.pushColor(Color_Brown);
				m_renderer.drawQuadFilled(quads[i].a, quads[i].b, quads[i].c, quads[i].d);
				m_renderer.popAlpha();
				m_renderer.popColor();
			}
		}

		m_renderer.drawPoint(Eigen::Vector3f(0, 0, 0), 7, boxCenter == m_renderer.getHotId() ? Color_Gold : Color_Green);
		m_renderer.drawLine(Eigen::Vector3f(-1, 0, 0), Eigen::Vector3f(1, 0, 0), 2, Color_White);
		m_renderer.drawLine(Eigen::Vector3f(0, -1, 0), Eigen::Vector3f(0, 1, 0), 2, Color_White);
		m_renderer.drawLine(Eigen::Vector3f(0, 0, -1), Eigen::Vector3f(0, 0, 1), 2, Color_White);
		if (boundingBox == m_renderer.getActiveId())
		{
			m_renderer.pushColor(Eigen::Vector4<uint8_t>(255, 82, 111, 108));

			m_renderer.pushAlpha(0.4);
			m_renderer.drawAlignedBoxFilled(Eigen::Vector3f(-1, -1, -1), Eigen::Vector3f(1, 1, 1));
			m_renderer.popAlpha();
			m_renderer.popColor();
		}

		m_renderer.pushSize(4.0);

		m_renderer.pushColor((boxCenter == m_renderer.getActiveId() || boundingBox == m_renderer.getActiveId()) ? Color_Green : Color_White);
		m_renderer.drawAlignedBox(Eigen::Vector3f(-1, -1, -1), Eigen::Vector3f(1, 1, 1));
		m_renderer.popColor();
		m_renderer.popSize();
		m_renderer.popMatrix();
		m_renderer.popId();

		return ret;
	}
	bool BoxEditGizmo::edit(unsigned int _id, Eigen::Vector3f& _translation_, Eigen::Matrix3f& _rotation_, Eigen::Vector3f& _scale_, bool translationFlag, bool rotateFlag, bool faceMeFlag, int& mode)
	{
		FrameParam& cameraParam = m_renderer.getFrameParam();
		bool ret = false;
		Eigen::Vector3f* outVec3 = new Eigen::Vector3f(0, 0, 0);
		*outVec3 = _translation_;
		Eigen::Vector3f drawAt = *outVec3;
		Eigen::Matrix3f* outMat3 = new Eigen::Matrix3f(_rotation_);
		Eigen::Matrix3f drawMat = *outMat3;
		Eigen::Vector3f* outScale = new Eigen::Vector3f(_scale_);
		Eigen::Vector3f drawScale = *outScale;

		Eigen::Vector3f xAxis((*outMat3)(0, 0), (*outMat3)(1, 0), (*outMat3)(2, 0));
		Eigen::Vector3f yAxis((*outMat3)(0, 1), (*outMat3)(1, 1), (*outMat3)(2, 1));
		Eigen::Vector3f zAxis((*outMat3)(0, 2), (*outMat3)(1, 2), (*outMat3)(2, 2));
		float worldHeight = m_renderer.pixelsToWorldSize(drawAt, 10);
		m_renderer.pushId(_id);
		m_renderer.setAppId(_id);
		unsigned int boxCenter = m_renderer.makeId("BoxCenter");
		unsigned int boundingBox = m_renderer.makeId("BoundingBox");
		bool canRotateBox = true;
		Ray camRay(cameraParam.rayOrigin, cameraParam.rayDirection);
		if (boundingBox != m_renderer.getActiveId() && Intersects(camRay, Sphere(drawAt, m_renderer.pixelsToWorldSize(drawAt, 15))))
		{
			canRotateBox = false;
		}
		Eigen::Vector3f viewNormal = cameraParam.viewDirectioin;
		if (translationFlag)
		{
			if (m_drag.planeTranslation(m_renderer, 
				boxCenter, drawAt, viewNormal, cameraParam.snapTranslation, worldHeight, outVec3))
			{
				ret = true;
				mode = 0;
			}
		}

		struct FaceCenter
		{
			unsigned int centerId;
			Eigen::Vector3f pos;
			Eigen::Vector3f nor;
			float size;
			float* outS;
			FaceCenter(const unsigned int& c, const Eigen::Vector3f& v, const Eigen::Vector3f& n, float* s)
				: centerId(c)
				, pos(v)
				, nor(n)
				, outS(s)
			{
			}
		};
		FaceCenter faces[6] = {
			{m_renderer.makeId("RightFaceCenter"), Eigen::Vector3f(drawAt + xAxis * drawScale.x()), xAxis, &(outScale->x())},
			{m_renderer.makeId("LeftFaceCenter"), Eigen::Vector3f(drawAt - xAxis * drawScale.x()), xAxis, &(outScale->x())},
			{m_renderer.makeId("TopFaceCenter"), Eigen::Vector3f(drawAt + yAxis * drawScale.y()), yAxis, &(outScale->y())},
			{m_renderer.makeId("BottomFaceCenter"), Eigen::Vector3f(drawAt - yAxis * drawScale.y()), yAxis, &(outScale->y())},
			{m_renderer.makeId("FrontCenter"), Eigen::Vector3f(drawAt + zAxis * drawScale.z()), zAxis, &(outScale->z())},
			{m_renderer.makeId("BackFaceCenter"), Eigen::Vector3f(drawAt - zAxis * drawScale.z()), zAxis, &(outScale->z())},
		};
		if (faceMeFlag)
		{
			for (int i = 0; i < 6; i++)
			{
				Eigen::Vector3f* outFace = new Eigen::Vector3f(0, 0, 0);
				*outFace = faces[i].pos;
				float size = m_renderer.pixelsToWorldSize(faces[i].pos, 10);
				if (m_drag.sphereAxisTranslation(m_renderer, 
					faces[i].centerId, faces[i].pos, size, faces[i].nor, cameraParam.snapTranslation, outFace))
				{
					ret = true;
					*outVec3 = *outVec3 + (*outFace - faces[i].pos) / 2;
					*faces[i].outS = (*outFace - *outVec3).norm();
					mode = 1;
				}
				if (boundingBox != m_renderer.getActiveId() && Intersects(camRay, Sphere(faces[i].pos, size)))
				{
					canRotateBox = false;
				}
				delete outFace;
			}

		}


		if (canRotateBox && rotateFlag)
		{
			Eigen::Matrix4f mat = Coord3(drawAt, drawMat, drawScale);
			Eigen::Matrix4f matinverse = mat.inverse();
			Eigen::Vector3f o = MatrixMulPoint(matinverse, cameraParam.rayOrigin);
			Eigen::Vector4<float> temp = matinverse * Eigen::Vector4<float>(cameraParam.rayDirection.x(),
				cameraParam.rayDirection.y(), cameraParam.rayDirection.z(), 0.0);
			Eigen::Vector3f d = temp.head<3>().normalized();
			Ray ray(o, d);
			float tr = FLT_MAX;
			bool hit = IntersectBox(ray, Eigen::Vector3f(-1, -1, -1), Eigen::Vector3f(1, 1, 1), tr);
			if (hit)
			{
				tr = (MatrixMulPoint(mat, ray.m_origin + ray.m_direction * tr) - cameraParam.rayOrigin).norm();
			}

			Eigen::Vector2<float>& storeCursor = m_drag.cursor();
			Eigen::Matrix3f& storeMat = m_drag.basis();

			if (boundingBox == m_renderer.getActiveId())
			{
				if (m_renderer.isKeyDown(ActionSelect))
				{
					Eigen::Vector2<float> offset;
					offset = 0.5 * (cameraParam.cursor - storeCursor) / 180.0 * Pi;
					offset.y() *= -1;

					Eigen::Matrix3f view = cameraParam.view.block(0, 0, 3, 3);
					Eigen::Vector3f axis = (view.row(1) * offset.x() - view.row(0) * offset.y()).normalized();

					Eigen::Vector2<float> debug = offset.normalized();

					Eigen::Vector3f s = drawAt + Eigen::Vector3f(100 * (view.row(0) * debug.y() - view.row(1) * debug.x()));
					Eigen::Vector3f e =
						drawAt + Eigen::Vector3f(100 * (-view.row(0) * debug.y() + view.row(1) * debug.x()));
					m_renderer.drawLine(s, e, 3, Color_Gold);
					*outMat3 = RotationMatrix(axis, offset.norm()) * storeMat;
					ret = true;
					mode = 2;
				}
				else
				{
					m_renderer.makeActive(IdInvalid);
				}
			}
			else if (boundingBox == m_renderer.getHotId())
			{
				if (hit)
				{
					if (m_renderer.isKeyDown(ActionSelect))
					{
						storeCursor = cameraParam.cursor;
						storeMat = *outMat3;
						m_renderer.makeActive(boundingBox);
					}
					m_renderer.updateDepth(tr);
				}
				else
				{
					m_renderer.resetId();
				}
			}
			else
			{
				m_renderer.makeHot(boundingBox, tr, hit);
			}
		}
		else if (boundingBox == m_renderer.getHotId())
		{
			m_renderer.resetId();
		}
		_translation_ = *outVec3;
		_rotation_ = *outMat3;
		_scale_ = *outScale;
		delete outVec3;
		delete outMat3;
		delete outScale;
		m_renderer.pushMatrix(Coord3(_translation_, _rotation_, _scale_));

		struct Quad
		{
			Eigen::Vector3f a;
			Eigen::Vector3f b;
			Eigen::Vector3f c;
			Eigen::Vector3f d;
		};

		Eigen::Vector3f points[6] = { Eigen::Vector3f(1, 0, 0), Eigen::Vector3f(-1, 0, 0), Eigen::Vector3f(0, 1, 0),
			Eigen::Vector3f(0, -1, 0), Eigen::Vector3f(0, 0, 1), Eigen::Vector3f(0, 0, -1) };
		Quad quads[6] = {
			{Eigen::Vector3f(1, -1, -1), Eigen::Vector3f(1, 1, -1), Eigen::Vector3f(1, 1, 1), Eigen::Vector3f(1, -1, 1)},
			{Eigen::Vector3f(-1, -1, -1), Eigen::Vector3f(-1, 1, -1), Eigen::Vector3f(-1, 1, 1),
				Eigen::Vector3f(-1, -1, 1)},
			{Eigen::Vector3f(-1, 1, -1), Eigen::Vector3f(1, 1, -1), Eigen::Vector3f(1, 1, 1), Eigen::Vector3f(-1, 1, 1)},
			{Eigen::Vector3f(-1, -1, -1), Eigen::Vector3f(1, -1, -1), Eigen::Vector3f(1, -1, 1),
				Eigen::Vector3f(-1, -1, 1)},
			{Eigen::Vector3f(-1, -1, 1), Eigen::Vector3f(1, -1, 1), Eigen::Vector3f(1, 1, 1), Eigen::Vector3f(-1, 1, 1)},
			{Eigen::Vector3f(-1, -1, -1), Eigen::Vector3f(1, -1, -1), Eigen::Vector3f(1, 1, -1), Eigen::Vector3f(-1, 1, -1)}

		};

		for (int i = 0; i < 6; i++)
		{
			m_renderer.drawPoint(points[i], 10, faces[i].centerId == m_renderer.getHotId() ? Color_Gold : Color_Green);
			if (faces[i].centerId == m_renderer.getHotId())
			{
				m_renderer.pushAlpha(0.3);
				m_renderer.pushColor(Color_Brown);
				m_renderer.drawQuadFilled(quads[i].a, quads[i].b, quads[i].c, quads[i].d);
				m_renderer.popAlpha();
				m_renderer.popColor();
			}
		}

		m_renderer.drawPoint(Eigen::Vector3f(0, 0, 0), 10, boxCenter == m_renderer.getHotId() ? Color_Gold : Color_Green);
		m_renderer.drawLine(Eigen::Vector3f(-1, 0, 0), Eigen::Vector3f(1, 0, 0), 2, Color_White);
		m_renderer.drawLine(Eigen::Vector3f(0, -1, 0), Eigen::Vector3f(0, 1, 0), 2, Color_White);
		m_renderer.drawLine(Eigen::Vector3f(0, 0, -1), Eigen::Vector3f(0, 0, 1), 2, Color_White);
		if (boundingBox == m_renderer.getActiveId())
		{
			m_renderer.pushColor(Eigen::Vector4<uint8_t>(255, 82, 111, 108));

			m_renderer.pushAlpha(0.4);
			m_renderer.drawAlignedBoxFilled(Eigen::Vector3f(-1, -1, -1), Eigen::Vector3f(1, 1, 1));
			m_renderer.popAlpha();
			m_renderer.popColor();
		}

		m_renderer.pushSize(4.0);

		m_renderer.pushColor((boxCenter == m_renderer.getActiveId() || boundingBox == m_renderer.getActiveId()) ? Color_Green : Color_White);
		m_renderer.drawAlignedBox(Eigen::Vector3f(-1, -1, -1), Eigen::Vector3f(1, 1, 1));
		m_renderer.popColor();
		m_renderer.popSize();
		m_renderer.popMatrix();
		m_renderer.popId();
		return ret;
	}
}
