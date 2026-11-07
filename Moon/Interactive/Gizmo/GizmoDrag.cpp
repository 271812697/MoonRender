#include "Interactive/Gizmo/GizmoDrag.h"

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
	bool GizmoDrag::spherePlaneTranslation(ImRenderer& p_renderer, unsigned _id, const Eigen::Vector3f& _origin, float _radius, const Eigen::Vector3f& _normal, float _snap, Eigen::Vector3f* _out_)
	{
		FrameParam& cameraParam = p_renderer.getFrameParam();
		Ray ray(cameraParam.rayOrigin, cameraParam.rayDirection);
		Plane plane(_normal, _origin);
		Sphere sphere(_origin, _radius);
		float tr = FLT_MAX, t0 = FLT_MAX, t1 = FLT_MAX;
		bool intersects = Intersect(ray, plane, tr) && Intersect(ray, sphere, t0, t1);
		Eigen::Vector3f intersection = ray.m_origin + ray.m_direction * tr;
		Eigen::Vector3f& storedPosition = m_position;
		if (_id == p_renderer.getActiveId())
		{
			if (p_renderer.isKeyDown(ActionSelect))
			{
				*_out_ = Snap(intersection + storedPosition, plane, _snap);
				return true;
			}
			else
			{
				p_renderer.makeActive(IdInvalid);
			}
		}
		else if (_id == p_renderer.getHotId())
		{
			if (intersects)
			{
				if (p_renderer.isKeyDown(ActionSelect))
				{
					p_renderer.makeActive(_id);
					storedPosition = *_out_ - intersection;
				}
			}
			else
			{
				p_renderer.resetId();
			}
		}
		else
		{
			p_renderer.makeHot(_id, t0, intersects);
		}
		return false;
	}
	bool GizmoDrag::circleAxisTranslation(ImRenderer& p_renderer, unsigned int _id, const Eigen::Vector3f& _origin, float _radius, const Eigen::Vector3f& _normal, float _snap, Eigen::Vector3f* _out_)
	{
		FrameParam& cameraParam = p_renderer.getFrameParam();
		Ray ray(cameraParam.rayOrigin, cameraParam.rayDirection);
		Plane plane(_normal, _origin);
		Line axisLine(_origin, _normal);

		float tr;
		bool intersects = Intersect(ray, plane, tr);

		Eigen::Vector3f intersection = ray.m_origin + ray.m_direction * tr;

		if ((intersection - _origin).norm() > _radius)
		{
			intersects = false;
		}
		Eigen::Vector3f& storedPosition = m_position;

		if (_id == p_renderer.getActiveId())
		{
			if (p_renderer.isKeyDown(ActionSelect))
			{
				float tr, tl;
				Nearest(ray, axisLine, tr, tl);
				*_out_ = Snap(*_out_ + _normal * tl - storedPosition, _snap);
				return true;
			}
			else
			{
				p_renderer.makeActive(IdInvalid);
			}
		}
		else if (_id == p_renderer.getHotId())
		{
			if (intersects)
			{

				p_renderer.updateDepth(tr);
				if (p_renderer.isKeyDown(ActionSelect))
				{
					p_renderer.makeActive(_id);
					float tr, t1;
					Nearest(ray, axisLine, tr, t1);
					storedPosition = _normal * t1;
				}
			}
			else
			{
				p_renderer.resetId();
			}
		}
		else
		{
			p_renderer.makeHot(_id, tr, intersects);
		}

		return false;
	}
	bool GizmoDrag::sphereAxisTranslation(ImRenderer& p_renderer, unsigned int _id, const Eigen::Vector3f& _origin, float _radius, const Eigen::Vector3f& _normal, float _snap, Eigen::Vector3f* _out_)
	{
		FrameParam& cameraParam = p_renderer.getFrameParam();
		Ray ray(cameraParam.rayOrigin, cameraParam.rayDirection);
		Sphere sphere(_origin, _radius);
		Line axisLine(_origin, _normal);

		float tr, t1;
		bool intersects = Intersect(ray, sphere, tr, t1);
		Eigen::Vector3f intersection = ray.m_origin + ray.m_direction * tr;
		Eigen::Vector3f& storedPosition = m_position;

		if (_id == p_renderer.getActiveId())
		{
			if (p_renderer.isKeyDown(ActionSelect))
			{
				float tr, tl;
				Nearest(ray, axisLine, tr, tl);
				*_out_ = Snap(*_out_ + _normal * tl - storedPosition, _snap);
				return true;
			}
			else
			{
				p_renderer.makeActive(IdInvalid);
			}
		}
		else if (_id == p_renderer.getHotId())
		{
			if (intersects)
			{
				if (p_renderer.isKeyDown(ActionSelect))
				{
					p_renderer.makeActive(_id);
					float tr, t1;
					Nearest(ray, axisLine, tr, t1);
					storedPosition = _normal * t1;
				}
			}
			else
			{
				p_renderer.resetId();
			}
		}
		else
		{
			p_renderer.makeHot(_id, tr, intersects);
		}

		return false;
	}
	bool GizmoDrag::sphereRotateInCircle(ImRenderer& p_renderer, unsigned int _id, const Eigen::Vector3f& _origin, float _radius, const Eigen::Vector3f& axis, Eigen::Vector3f* _out_, bool inPlane)
	{
		FrameParam& cameraParam = p_renderer.getFrameParam();
		Ray ray(cameraParam.rayOrigin, cameraParam.rayDirection);
		Sphere sphere(*_out_, _radius);
		float a = (sphere.m_origin - _origin).dot(axis);
		float b= (sphere.m_origin - _origin).norm();
		float tr, t1;
		bool intersects = Intersect(ray, sphere, tr, t1);
		if (_id == p_renderer.getActiveId())
		{
			if (p_renderer.isKeyDown(ActionSelect))
			{
				auto p=inPlane? _origin :_origin + axis * m_offset.x();
				Plane plane(axis, p);

				float v;
				bool intersects = Intersect(ray, plane, v);

				Eigen::Vector3f intersection = ray.m_origin + ray.m_direction *v;
				*_out_= (intersection - p).normalized()* m_offset.y()+p;
				
				return true;
			}
			else
			{
				p_renderer.makeActive(IdInvalid);
			}
		}
		else if (_id == p_renderer.getHotId())
		{
			if (intersects)
			{
				p_renderer.updateDepth(tr);
				if (p_renderer.isKeyDown(ActionSelect))
				{
					p_renderer.makeActive(_id);
					float offset = (sphere.m_origin - _origin).dot(axis);
					m_offset .x() = inPlane?0: offset;
					
					{
						// compute radius in plane perpendicular to axis
						float d = (sphere.m_origin - _origin).norm();
						m_offset.y() = sqrtf(fabs(d * d - offset * offset));
					}

				}
			}
			else
			{
				p_renderer.resetId();
			}
		}
		else
		{
			p_renderer.makeHot(_id, tr, intersects);
		}
		return false;
	}
	bool GizmoDrag::operateNormal(ImRenderer& p_renderer, unsigned int _id, const Eigen::Vector3f& _origin, const Eigen::Vector3f& _end, float _worldSize, Eigen::Vector3f* _out_)
	{
		FrameParam& cameraParam = p_renderer.getFrameParam();
		bool ret = false;
		// Operate normal
		Ray ray(cameraParam.rayOrigin, cameraParam.rayDirection);

		Capsule axisCapsule(_origin, _end, _worldSize);

		float t0 = FLT_MAX, t1 = FLT_MAX;
		bool intersects = Intersect(ray, axisCapsule, t0, t1); //

		Eigen::Vector2f& storeCursor = m_cursor;
		Eigen::Vector3f& storeNormal = m_normal;

		if (_id == p_renderer.getActiveId())
		{
			if (p_renderer.isKeyDown(ActionSelect))
			{
				Eigen::Vector2<float> offset = 0.5 * (cameraParam.cursor - storeCursor) / 180.0 * Pi;
				offset.y() *= -1;
				Eigen::Matrix3f view = cameraParam.view.block(0, 0, 3, 3);
				Eigen::Vector3f axis = (view.row(1) * offset.x() - view.row(0) * offset.y()).normalized();
				Eigen::Matrix3f mat = RotationMatrix(axis, offset.norm());
				*_out_ = (mat * storeNormal).normalized();
				ret = true;
			}
			else
			{
				p_renderer.makeActive(IdInvalid);
			}
		}
		else if (_id == p_renderer.getHotId())
		{
			if (intersects)
			{
				if (p_renderer.isKeyDown(ActionSelect))
				{

					storeCursor = cameraParam.cursor;
					storeNormal = (_end - _origin).normalized();
					p_renderer.makeActive(_id);
				}
			}
			else
			{
				p_renderer.resetId();
			}
		}
		else
		{

			if (intersects)
			{
				p_renderer.makeHot(_id, t0, intersects);
			}
		}
		return ret;
	}
	bool GizmoDrag::axisTranslation(ImRenderer& p_renderer, unsigned int _id, const Eigen::Vector3f& _origin, const Eigen::Vector3f& _axis, float _snap, float _worldHeight, float _worldSize, Eigen::Vector3f* _out_)
	{
		FrameParam& cameraParam = p_renderer.getFrameParam();

		if (_id != p_renderer.getHotId())
		{
			// disable behavior when aligned
			Eigen::Vector3f viewDir =
				cameraParam.orthProj ? cameraParam.viewDirectioin : (cameraParam.eye - _origin).normalized();
			float aligned = 1.0f - fabs(_axis.dot(viewDir));
			if (aligned < 0.01f)
			{
				return false;
			}
		}

		Ray ray(cameraParam.rayOrigin, cameraParam.rayDirection);
		Line axisLine(_origin, _axis);
		Capsule axisCapsule(_origin + _axis * (0.2f * _worldHeight), _origin + _axis * _worldHeight, _worldSize);

		Eigen::Vector3f& storedPosition = m_position;

		if (_id == p_renderer.getActiveId())
		{
			if (p_renderer.isKeyDown(ActionSelect))
			{
				float tr, tl;
				Nearest(ray, axisLine, tr, tl);

				*_out_ = Snap(*_out_ + _axis * tl - storedPosition, _snap);

				return true;
			}
			else
			{
				p_renderer.makeActive(IdInvalid);
			}
		}
		else if (_id == p_renderer.getHotId())
		{
			if (Intersects(ray, axisCapsule))
			{
				if (p_renderer.isKeyDown(ActionSelect))
				{
					p_renderer.makeActive(_id);
					float tr, tl;
					Nearest(ray, axisLine, tr, tl);
					storedPosition = _axis * tl;
				}
			}
			else
			{
				p_renderer.resetId();
			}
		}
		else
		{
			float t0, t1;
			bool intersects = Intersect(ray, axisCapsule, t0, t1);
			if (p_renderer.makeHot(_id, t0, intersects))
			{
			}
		}
		return false;
	}
	bool GizmoDrag::planeTranslation(ImRenderer& p_renderer, unsigned int _id, const Eigen::Vector3f& _origin, const Eigen::Vector3f& _normal, float _snap, float _worldSize, Eigen::Vector3f* _out_)
	{
		FrameParam& cameraParam = p_renderer.getFrameParam();
		Ray ray(cameraParam.rayOrigin, cameraParam.rayDirection);
		Plane plane(_normal, _origin);

		float tr;
		bool intersects = Intersect(ray, plane, tr);
		if (!intersects)
		{
			return false;
		}
		Eigen::Vector3f intersection = ray.m_origin + ray.m_direction * tr;
		Eigen::Vector3f Abs = Eigen::Vector3f(
			abs(intersection.x() - _origin.x()), abs(intersection.y() - _origin.y()), abs(intersection.z() - _origin.z()));
		bool allLessFlag = Abs.x() < _worldSize && Abs.y() < _worldSize && Abs.z() < _worldSize;
		intersects &= allLessFlag;
		Eigen::Vector3f& storedPosition = m_position;

		if (_id == p_renderer.getActiveId())
		{
			if (p_renderer.isKeyDown(ActionSelect))
			{
				*_out_ = Snap(intersection + storedPosition, plane, _snap);
				return true;
			}
			else
			{
				p_renderer.makeActive(IdInvalid);
			}
		}
		else if (_id == p_renderer.getHotId())
		{
			if (intersects)
			{
				if (p_renderer.isKeyDown(ActionSelect))
				{
					p_renderer.makeActive(_id);
					storedPosition = *_out_ - intersection;
				}
			}
			else
			{
				p_renderer.resetId();
			}
		}
		else
		{
			p_renderer.makeHot(_id, tr, intersects);
		}
		return false;
	}
	bool GizmoDrag::axisAngle(ImRenderer& p_renderer, unsigned int _id, const Eigen::Vector3f& _origin, const Eigen::Vector3f& _axis, float _snap, float _worldRadius, float _worldSize, float* _out_)
	{
		FrameParam& cameraParam = p_renderer.getFrameParam();
		Eigen::Vector3f viewDir =
			cameraParam.orthProj ? cameraParam.viewDirectioin : (cameraParam.eye - _origin).normalized();

		float aligned = fabs(_axis.dot(viewDir));
		float tr = 0.0f;
		Ray ray(cameraParam.rayOrigin, cameraParam.rayDirection);
		bool intersects = false;
		Eigen::Vector3f intersection;
		if (aligned < 0.05f)
		{
			// ray-plane intersection fails at grazing angles, use capsule interesection
			float t1;
			Eigen::Vector3f capsuleAxis = viewDir.cross(_axis);
			Capsule capsule(_origin + capsuleAxis * _worldRadius, _origin - capsuleAxis * _worldRadius, _worldSize * 0.5f);
			intersects = Intersect(ray, capsule, tr, t1);
			intersection = ray.m_origin + ray.m_direction * tr;
		}
		else
		{
			Plane plane(_axis, _origin);
			intersects = Intersect(ray, plane, tr);
			intersection = ray.m_origin + ray.m_direction * tr;
			float dist = (intersection - _origin).norm();
			intersects &= fabs(dist - _worldRadius) < (_worldSize + _worldSize * (1.0f - aligned) * 2.0f);
		}

		Eigen::Vector3f& storedVec = m_position;
		float& storedAngle = m_angle;
		bool ret = false;

		// use a view-aligned plane intersection to generate the rotation delta
		Plane viewPlane(viewDir, _origin);
		Intersect(ray, viewPlane, tr);
		intersection = ray.m_origin + ray.m_direction * tr;

		if (_id == p_renderer.getActiveId())
		{
			if (p_renderer.isKeyDown(ActionSelect))
			{
				Eigen::Vector3f delta = (intersection - _origin).normalized();
				float sign = storedVec.cross(delta).dot(_axis);
				float angle = acosf(Clamp(delta.dot(storedVec), -1.0f, 1.0f));
				
				*_out_ = Snap(storedAngle + copysignf(angle, sign), _snap);
				//*_out_ = Snap(*_out_ + copysignf(angle, sign), _snap);
				//storedVec = delta;
				return true;
			}
			else
			{
				p_renderer.makeActive(IdInvalid);
			}
		}
		else if (_id == p_renderer.getHotId())
		{
			if (intersects)
			{
				if (p_renderer.isKeyDown(ActionSelect))
				{
					p_renderer.makeActive(_id);
					storedVec = (intersection - _origin).normalized();
					m_startDirection = storedVec;
					storedAngle = Snap(*_out_, cameraParam.snapRotation);
				}
			}
			else
			{
				p_renderer.resetId();
			}
		}
		else
		{
			p_renderer.makeHot(_id, tr, intersects);
		}
		return false;
	}
}
