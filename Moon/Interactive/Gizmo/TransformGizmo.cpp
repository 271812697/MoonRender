#include "Interactive/Gizmo/TransformGizmo.h"

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
	TransformGizmo::TransformGizmo(ImRenderer& p_renderer) : m_renderer(p_renderer) { }

	bool TransformGizmo::manipulate(unsigned _id, Eigen::Matrix4f& model)
	{
		FrameParam& cameraParam = m_renderer.getFrameParam();
		syncModeFromKeys();
		// Draw Ray hit point
		m_renderer.pushColor(Eigen::Vector4<uint8_t>(255, 0, 0, 255));
		m_renderer.pushMatrix(Eigen::Matrix4f::Identity());
		m_renderer.drawSphereFilled(cameraParam.rayOrigin + cameraParam.rayDirection,
			m_renderer.pixelsToWorldSize(cameraParam.rayOrigin + cameraParam.rayDirection, 3.0));
		m_renderer.popMatrix();
		m_renderer.popColor();

		m_renderer.pushMatrix(model);
		bool ret = false;
		Eigen::Vector3f position = Eigen::Vector3f(model(0, 3), model(1, 3), model(2, 3));
		Eigen::Matrix3f rot = model.block(0, 0, 3, 3);
		switch (m_mode)
		{
		case GizmoModeTranslation:

			if (translation(_id, position, m_local))
			{
				model.block(0, 3, 3, 1) = position;
				ret = true;
			}
			break;
		case GizmoModeRotation: {

			if (rotation(_id, rot, m_local))
			{
				model.block(0, 0, 3, 3) = rot;
				ret = true;
			}
			break;
		}

		case GizmoModeScale:
			break;
		default:
			break;
		}
		m_renderer.popMatrix();
		return ret;
	}
	bool TransformGizmo::manipulate(const char* _id, Eigen::Matrix4f& model)
	{
		return manipulate(m_renderer.makeId(_id), model);
	}
	bool TransformGizmo::translation(unsigned int _id, Eigen::Vector3f& _translation_, bool _local)
	{
		FrameParam& cameraParam = m_renderer.getFrameParam();
		bool ret = false;
		Eigen::Vector3f* outVec3 = new Eigen::Vector3f(_translation_.x(), _translation_.y(), _translation_.z());
		Eigen::Vector3f drawAt = *outVec3;

		float worldHeight = m_renderer.pixelsToWorldSize(drawAt, Gizmo::kHandleHeightPixels);
		m_renderer.pushId(_id);
		m_renderer.setAppId(_id);
		if (_local)
		{
			Eigen::Matrix4f localMatrix = m_renderer.getMatrix();

			m_renderer.pushMatrix(localMatrix);
		}
		float planeSize = worldHeight * (0.5f * 0.5f);
		float planeOffset = worldHeight * 0.5f;
		float worldSize = m_renderer.pixelsToWorldSize(drawAt, Gizmo::kHandleLinePixels);

		struct AxisG
		{
			unsigned int m_id;
			Eigen::Vector3f m_axis;
			Eigen::Vector4<uint8_t> m_color;
		};
		AxisG axes[] = { {m_renderer.makeId("axisX"), Eigen::Vector3f(1.0f, 0.0f, 0.0f), Eigen::Vector4<uint8_t>(255, 0, 0, 255)},
			{m_renderer.makeId("axisY"), Eigen::Vector3f(0.0f, 1.0f, 0.0f), Eigen::Vector4<uint8_t>(255, 0, 255, 0)},
			{m_renderer.makeId("axisZ"), Eigen::Vector3f(0.0f, 0.0f, 1.0f), Eigen::Vector4<uint8_t>(255, 255, 0, 0)} };
		struct PlaneG
		{
			unsigned int m_id;
			Eigen::Vector3f m_origin;
		};

		PlaneG planes[] = { {m_renderer.makeId("planeYZ"), Eigen::Vector3f(0.0f, planeOffset, planeOffset)},
			{m_renderer.makeId("planeXZ"), Eigen::Vector3f(planeOffset, 0.0f, planeOffset)},
			{m_renderer.makeId("planeXY"), Eigen::Vector3f(planeOffset, planeOffset, 0.0f)},
			{m_renderer.makeId("planeV"), Eigen::Vector3f(0.0f, 0.0f, 0.0f)} };

		if (cameraParam.flipGizmoWhenBehind)
		{
			const Eigen::Vector3f viewDir =
				cameraParam.orthProj ? -cameraParam.viewDirectioin : (cameraParam.eye - *outVec3).normalized();

			for (int i = 0; i < 3; ++i)
			{
				const Eigen::Vector3f axis =
					_local ? Eigen::Vector3f(m_renderer.getMatrix().block(0, i, 3, 1)) : axes[i].m_axis;
				if (axis.dot(viewDir) < 0.0f)
				{
					axes[i].m_axis = -axes[i].m_axis;
					for (int j = 0; j < 3; ++j)
					{
						planes[j].m_origin[i] = -planes[j].m_origin[i];
					}
				}
			}
		}
		Sphere boundingSphere(*outVec3, worldHeight * 1.5f); // expand the bs to catch the planar subgizmos
		Ray ray(cameraParam.rayOrigin, cameraParam.rayDirection);
		bool intersects = m_renderer.isAppHot() || Intersects(ray, boundingSphere);
		// pushEnableSorting(true);

		if (_local)
		{
			// local planes need to be drawn with the pushed matrix for correct orientation
			for (int i = 0; i < 3; ++i)
			{
				const PlaneG& plane = planes[i];
				Gizmo::DrawPlaneTranslationHandle(m_renderer, plane.m_id, plane.m_origin, axes[i].m_axis, planeSize, Color_Gold);
				axes[i].m_axis = m_renderer.getMatrix().block(0, 0, 3, 3) * axes[i].m_axis;
				if (intersects)
				{
					ret |= m_drag.planeTranslation(m_renderer, plane.m_id, MatrixMulPoint(m_renderer.getMatrix(), plane.m_origin),
						axes[i].m_axis, cameraParam.snapTranslation, planeSize, outVec3);
				}
			}
		}
		else
		{
			m_renderer.pushMatrix(Eigen::Matrix4f::Identity());
			for (int i = 0; i < 3; ++i)
			{
				const PlaneG& plane = planes[i];
				Gizmo::DrawPlaneTranslationHandle(m_renderer, plane.m_id, drawAt + plane.m_origin, axes[i].m_axis, planeSize, Color_Gold);
				if (intersects)
				{
					ret |= m_drag.planeTranslation(m_renderer, plane.m_id, drawAt + plane.m_origin, axes[i].m_axis,
						cameraParam.snapTranslation, planeSize, outVec3);
				}
			}
			m_renderer.popMatrix();
		}
		m_renderer.pushMatrix(Eigen::Matrix4f::Identity());

		if (intersects)
		{
			// view plane (store the normal when the gizmo becomes active)
			unsigned int currentId = m_renderer.getActiveId();
			Eigen::Vector3f storedViewNormal = m_drag.basis().col(0);
			Eigen::Vector3f viewNormal;
			if (planes[3].m_id == m_renderer.getActiveId())
			{
				viewNormal = storedViewNormal;
			}
			else
			{
				viewNormal = cameraParam.viewDirectioin;
			}
			ret |= m_drag.planeTranslation(m_renderer, 
				planes[3].m_id, drawAt, viewNormal, cameraParam.snapTranslation, worldSize, outVec3);
			if (currentId != m_renderer.getActiveId())
			{
				// gizmo became active, store the view normal
				storedViewNormal = viewNormal;
				m_drag.basis().block(0, 0, 3, 1) = storedViewNormal;
			}

			// highlight axes if the corresponding plane is hot
			if (planes[0].m_id == m_renderer.getHotId()) // YZ
			{
				axes[1].m_color = axes[2].m_color = Color_Gold;
			}
			else if (planes[1].m_id == m_renderer.getHotId()) // XZ
			{
				axes[0].m_color = axes[2].m_color = Color_Gold;
			}
			else if (planes[2].m_id == m_renderer.getHotId()) // XY
			{
				axes[0].m_color = axes[1].m_color = Color_Gold;
			}
			else if (planes[3].m_id == m_renderer.getHotId()) // view plane
			{
				axes[0].m_color = axes[1].m_color = axes[2].m_color = Color_Gold;
			}
		}

		// draw the view plane handle
		m_renderer.begin(PrimitiveModePoints);
		m_renderer.vertex(drawAt, Gizmo::kHandleLinePixels * 2.0f,
			planes[3].m_id == m_renderer.getHotId() ? Color_Gold : Eigen::Vector4<uint8_t>(255, 255, 255, 255));
		m_renderer.end();
		// axes
		for (int i = 0; i < 3; ++i)
		{
			AxisG& axis = axes[i];
			Gizmo::DrawAxisTranslationHandle(m_renderer, axis.m_id, drawAt, axis.m_axis, worldHeight, worldSize, axis.m_color);
			if (intersects)
			{

				ret |= m_drag.axisTranslation(m_renderer, 
					axis.m_id, drawAt, axis.m_axis, cameraParam.snapTranslation, worldHeight, worldSize, outVec3);
			}
		}
		m_renderer.popMatrix();
		// popEnableSorting();

		if (_local)
		{
			m_renderer.popMatrix();
		}

		m_renderer.popId();
		_translation_ = *outVec3;
		delete outVec3;
		return ret;
	}
	bool TransformGizmo::rotation(unsigned int _id, Eigen::Matrix3f& _rotation_, bool _local)
	{
		FrameParam& cameraParam = m_renderer.getFrameParam();
		Eigen::Vector3f origin = m_renderer.getMatrix().block(0, 3, 3, 1);
		float worldRadius = m_renderer.pixelsToWorldSize(origin, Gizmo::kHandleHeightPixels);
		unsigned int currentId = m_renderer.getActiveId(); // store currentId to detect if the gizmo becomes active during this call
		m_renderer.pushId(_id);
		m_renderer.setAppId(_id);

		bool ret = false;
		Eigen::Matrix3f& storedRotation = m_drag.basis();
		Eigen::Matrix3f* outMat3 = new Eigen::Matrix3f(_rotation_);
		Eigen::Vector3f euler = ToEulerXYZ(*outMat3);
		float worldSize = m_renderer.pixelsToWorldSize(origin, Gizmo::kHandleLinePixels);
		struct AxisG
		{
			unsigned int m_id;
			Eigen::Vector3f m_axis;
			Eigen::Vector4<uint8_t> m_color;
		};
		AxisG axes[] = { {m_renderer.makeId("axisX"), Eigen::Vector3f(1.0f, 0.0f, 0.0f), Eigen::Vector4<uint8_t>(255, 0, 0, 255)},
			{m_renderer.makeId("axisY"), Eigen::Vector3f(0.0f, 1.0f, 0.0f), Eigen::Vector4<uint8_t>(255, 0, 255, 0)},
			{m_renderer.makeId("axisZ"), Eigen::Vector3f(0.0f, 0.0f, 1.0f), Eigen::Vector4<uint8_t>(255, 255, 0, 0)} };
		unsigned int viewId = m_renderer.makeId("axisV");

		Sphere boundingSphere(origin, worldRadius);
		Ray ray(cameraParam.rayOrigin, cameraParam.rayDirection);
		bool intersects = m_renderer.isAppHot() || Intersects(ray, boundingSphere);

		// const AppData& appData = ctx.getAppData();
		if (_local)
		{
			// extract axes from the pushed matrix
			for (int i = 0; i < 3; ++i)
			{
				if (m_renderer.getActiveId() == axes[i].m_id)
				{
					// use the stored matrix where the id is active, avoid rotating the axis frame during interaction (cause
					// numerical instability)
					axes[i].m_axis = storedRotation.col(i).normalized(); // Normalize( Eigen::Vector3f(storedRotation.getCol(i)));
				}
				else
				{
					// matrixStack.back().col(i).normalized();
					axes[i].m_axis = m_renderer.getMatrix().block(0, i, 3, 1).normalized();
				}
			}
		}

		m_renderer.pushMatrix(Eigen::Matrix4f::Identity());
		// ctx.pushMatrix(Mat4(1.0f));
		for (int i = 0; i < 3; ++i)
		{
			if (i == 0 && (m_renderer.getActiveId() == axes[1].m_id || m_renderer.getActiveId() == axes[2].m_id || m_renderer.getActiveId() == viewId))
			{
				continue;
			}
			if (i == 1 && (m_renderer.getActiveId() == axes[2].m_id || m_renderer.getActiveId() == axes[0].m_id || m_renderer.getActiveId() == viewId))
			{
				continue;
			}
			if (i == 2 && (m_renderer.getActiveId() == axes[0].m_id || m_renderer.getActiveId() == axes[1].m_id || m_renderer.getActiveId() == viewId))
			{
				continue;
			}

			AxisG& axis = axes[i];
			Gizmo::DrawAngleRing(m_renderer, m_drag, axis.m_id, origin, axis.m_axis, worldRadius * 0.9f, euler[i], axis.m_color, 0.0f);
			if (intersects && m_drag.axisAngle(m_renderer, axis.m_id, origin, axis.m_axis, cameraParam.snapRotation,
				worldRadius * 0.9f, worldSize, &euler[i]))
			{
				*outMat3 = RotationMatrix(axis.m_axis, euler[i] - m_drag.angle()) * storedRotation;
				ret = true;
			}
		}
		if (!(m_renderer.getActiveId() == axes[0].m_id || m_renderer.getActiveId() == axes[1].m_id || m_renderer.getActiveId() == axes[2].m_id))
		{
			Eigen::Vector3f viewNormal = cameraParam.viewDirectioin;
			float angle = 0.0f;
			if (intersects && m_drag.axisAngle(m_renderer, 
				viewId, origin, viewNormal, cameraParam.snapRotation, worldRadius, worldSize, &angle))
			{
				*outMat3 = RotationMatrix(viewNormal, angle) * storedRotation;
				ret = true;
			}
			Gizmo::DrawAngleRing(m_renderer, m_drag, viewId, origin, viewNormal, worldRadius, angle,
				viewId == m_renderer.getActiveId() ? Color_Gold : Eigen::Vector4<uint8_t>(255, 255, 255, 255), 1.0f);
		}
		m_renderer.popMatrix();

		if (currentId != m_renderer.getActiveId())
		{
			// gizmo became active, store rotation matrix
			storedRotation = *outMat3;
		}
		m_renderer.popId();
		_rotation_ = *outMat3;
		delete outMat3;

		return ret;
	}

	void TransformGizmo::syncModeFromKeys()
	{
		// 等价于原来 ImRenderer::newFrame 里的模式键处理；按键边沿状态由渲染器维护，
		// 这里只读一次，所以一帧内多次进入交互也只切换一次。
		if (m_renderer.wasKeyPressed(ActionGizmoTranslation))
		{
			m_mode = GizmoModeTranslation;
			m_renderer.resetId();
		}
		else if (m_renderer.wasKeyPressed(ActionGizmoRotation))
		{
			m_mode = GizmoModeRotation;
			m_renderer.resetId();
		}
		else if (m_renderer.wasKeyPressed(ActionGizmoScale))
		{
			m_mode = GizmoModeScale;
			m_renderer.resetId();
		}
		if (m_renderer.wasKeyPressed(ActionGizmoLocal))
		{
			m_local = !m_local;
			m_renderer.resetId();
		}
	}
}
