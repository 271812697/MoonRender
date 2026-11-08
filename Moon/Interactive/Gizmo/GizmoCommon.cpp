#include "Interactive/Gizmo/GizmoCommon.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <vector>
#include <Eigen/Core>

#include "Interactive/Im3DRenderer.h"
#include "Interactive/Im3DPalette.h"
#include "Interactive/MathUtil/MathUtil.h"
#include "Interactive/Gizmo/GizmoDrag.h"

namespace MOON::Gizmo
{
	void DrawAxisArrow(ImRenderer& p_renderer, unsigned int _id, const Eigen::Vector3f& _origin, const Eigen::Vector3f& _axis, float _worldHeight, Eigen::Vector4<uint8_t> _color)
	{
		FrameParam& cameraParam = p_renderer.getFrameParam();

		Eigen::Vector3f viewDir =
			cameraParam.orthProj ? cameraParam.viewDirectioin : (cameraParam.eye - _origin);

		Eigen::Vector4<uint8_t> color = _color;
		if (_id == p_renderer.getActiveId())
		{
			color = Color_Gold;

			p_renderer.begin(PrimitiveModeLines);
			p_renderer.vertex(_origin - _axis * 999.0f, Gizmo::kHandleLinePixels * 0.5f, _color);
			p_renderer.vertex(_origin + _axis * 999.0f, Gizmo::kHandleLinePixels * 0.5f, _color);
			p_renderer.end();

		}
		else if (_id == p_renderer.getHotId())
		{
			color = Color_Gold;
		}
		p_renderer.pushColor(color);
		p_renderer.pushSize(2.0);

		p_renderer.drawArrow(_origin + _axis * _worldHeight * 0.1, _origin + _axis * _worldHeight, -1, 8);
		p_renderer.popSize();
		p_renderer.popColor();
	}
	void DrawRayHitScreenPoint(ImRenderer& p_renderer)
	{
		FrameParam& cameraParam = p_renderer.getFrameParam();
		// Draw Ray hit point
		p_renderer.pushColor(Eigen::Vector4<uint8_t>(255, 0, 200, 255));
		p_renderer.pushMatrix(Eigen::Matrix4f::Identity());
		p_renderer.drawPoint(cameraParam.rayOrigin + cameraParam.rayDirection, 8, (Eigen::Vector4<uint8_t>(255, 0, 200, 255)));

		p_renderer.popMatrix();
		p_renderer.popColor();
	}
	void DrawAxisTranslationHandle(ImRenderer& p_renderer, unsigned int _id, const Eigen::Vector3f& _origin, const Eigen::Vector3f& _axis, float _worldHeight, float _worldSize, Eigen::Vector4<uint8_t> _color)
	{
		FrameParam& cameraParam = p_renderer.getFrameParam();
		Eigen::Vector3f viewDir =
			cameraParam.orthProj ? cameraParam.viewDirectioin : (cameraParam.eye - _origin).normalized();
		;
		float aligned = 1.0f - fabs(_axis.dot(viewDir));
		aligned = Remap(aligned, 0.05f, 0.1f);
		Eigen::Vector4<uint8_t> color = _color;
		if (_id == p_renderer.getActiveId())
		{
			color = Color_Gold;
			p_renderer.pushEnableSorting(false);
			p_renderer.begin(PrimitiveModeLines);
			p_renderer.vertex(_origin - _axis * 999.0f, Gizmo::kHandleLinePixels * 0.5f, _color);
			p_renderer.vertex(_origin + _axis * 999.0f, Gizmo::kHandleLinePixels * 0.5f, _color);
			p_renderer.end();
			p_renderer.popEnableSorting();
		}
		else if (_id == p_renderer.getHotId())
		{
			color = Color_Gold;
			aligned = 1.0f;
		}
		color(0) = color(0) * aligned; // .setA(color.getA() * aligned);
		p_renderer.pushColor(color);
		p_renderer.pushSize(Gizmo::kHandleLinePixels*3);

		p_renderer.drawArrow(_origin + _axis * (0.2f * _worldHeight), _origin + _axis * _worldHeight);
		p_renderer.popSize();
		p_renderer.popColor();
	}
	void DrawPlaneTranslationHandle(ImRenderer& p_renderer, unsigned int _id, const Eigen::Vector3f& _origin, const Eigen::Vector3f& _normal, float _worldSize, Eigen::Vector4<uint8_t> _color)
	{
		FrameParam& cameraParam = p_renderer.getFrameParam();
		Eigen::Vector3f viewDir =
			cameraParam.orthProj ? cameraParam.viewDirectioin : (cameraParam.eye - _origin).normalized();

		Eigen::Vector3f n =
			p_renderer.getMatrix().block(0, 0, 3, 3) * _normal; // _normal may be in local space, need to transform to world
		// space for the dot with viewDir to make sense
		float aligned = fabs(n.dot(viewDir));
		aligned = Remap(aligned, 0.1f, 0.2f);
		Eigen::Vector4<uint8_t> color = _color;
		color(0) = (color(0) * aligned);
		p_renderer.pushColor(color);
		p_renderer.pushAlpha(_id == p_renderer.getHotId() ? 0.7f : 0.1f * p_renderer.getAlpha());

		p_renderer.drawQuadFilled(_origin, _normal, Eigen::Vector2<float>(_worldSize, _worldSize));
		p_renderer.popAlpha();
		p_renderer.drawQuad(_origin, _normal, Eigen::Vector2<float>(_worldSize, _worldSize));
		p_renderer.popColor();
	}
	void DrawRotationRing(ImRenderer& p_renderer, unsigned int _id, const Eigen::Vector3f& _origin, const Eigen::Vector3f& _axis, float _worldRadius, Eigen::Vector4<uint8_t> _color)
	{
		FrameParam& cameraParam = p_renderer.getFrameParam();
		Eigen::Vector3f viewDir = cameraParam.orthProj ? cameraParam.viewDirectioin : (cameraParam.eye - _origin).normalized();
		Eigen::Vector4<uint8_t> color = _color;

		if (_id == p_renderer.getActiveId())
		{
			color = Color_Green;
			Ray ray(cameraParam.rayOrigin, cameraParam.rayDirection);
			Plane plane(_axis, _origin);
			float tr;
			if (Intersect(ray, plane, tr))
			{
				Eigen::Vector3f intersection = ray.m_origin + ray.m_direction * tr;
				Eigen::Vector3f delta = (intersection - _origin).normalized();

				p_renderer.pushColor(Color_Gold);
				p_renderer.pushSize(Gizmo::kHandleLinePixels);
				p_renderer.drawArrow(_origin, _origin + delta * _worldRadius);
				p_renderer.popSize();
				p_renderer.popColor();

				p_renderer.begin(PrimitiveModePoints);
				p_renderer.vertex(_origin, Gizmo::kHandleLinePixels * 2.0f, Color_Gold);
				p_renderer.end();
			}
		}
		else if (_id == p_renderer.getHotId())
		{
			color = Color_Green;
		}

		p_renderer.pushColor(color);
		p_renderer.pushSize(Gizmo::kHandleLinePixels);
		p_renderer.pushMatrix(p_renderer.getMatrix() * LookAt(_origin, _origin + _axis));
		// pushMatrix(getMatrix() * LookAt(_origin, _origin + _axis, m_appData.m_worldUp));
		p_renderer.begin(PrimitiveModeLineLoop);
		const int detail = p_renderer.estimateLevelOfDetail(_origin, _worldRadius, 32, 128);
		for (int i = 0; i < detail; ++i)
		{
			float rad = TwoPi * ((float)i / (float)detail);
			p_renderer.vertex(Eigen::Vector3f(cosf(rad) * _worldRadius, sinf(rad) * _worldRadius, 0.0f));
		}
		p_renderer.end();
		p_renderer.pushAlpha(0.3);
		p_renderer.drawCircleFilled({ 0,0,0 }, { 0, 0, 1 }, _worldRadius, 20);
		p_renderer.popAlpha();
		p_renderer.popMatrix();
		p_renderer.popSize();
		p_renderer.popColor();
	}
	void DrawAngleRing(ImRenderer& p_renderer, GizmoDrag& p_drag, unsigned int _id, const Eigen::Vector3f& _origin, const Eigen::Vector3f& _axis, float _worldRadius, float _angle, Eigen::Vector4<uint8_t> _color, float _minAlpha)
	{
		FrameParam& cameraParam = p_renderer.getFrameParam();
		Eigen::Vector3f viewDir = cameraParam.orthProj ? cameraParam.viewDirectioin : (cameraParam.eye - _origin).normalized();
		float aligned = fabs(_axis.dot(viewDir));

		Eigen::Vector3f& storedVec = p_drag.position();
		Eigen::Vector4<uint8_t> color = _color;

		if (_id == p_renderer.getActiveId())
		{
			color = Color_Gold;
			Ray ray(cameraParam.rayOrigin, cameraParam.rayDirection);
			Plane plane(_axis, _origin);
			float tr;
			if (Intersect(ray, plane, tr))
			{
				Eigen::Vector3f intersection = ray.m_origin + ray.m_direction * tr;
				Eigen::Vector3f delta = (intersection - _origin).normalized();

				p_renderer.pushAlpha(std::max(_minAlpha, Remap(aligned, 1.0f, 0.99f)));


				p_renderer.begin(PrimitiveModeLines);
				p_renderer.vertex(_origin - _axis * 999.0f, Gizmo::kHandleLinePixels * 0.5f, _color);
				p_renderer.vertex(_origin + _axis * 999.0f, Gizmo::kHandleLinePixels * 0.5f, _color);
				p_renderer.end();

				p_renderer.popAlpha();
				p_renderer.pushColor(Color_Gold);
				p_renderer.pushSize(Gizmo::kHandleLinePixels);
				p_renderer.drawArrow(_origin, _origin + delta * _worldRadius);
				p_renderer.drawArrow(_origin, _origin + p_drag.position().normalized() * _worldRadius);
				
				p_renderer.popSize();
				p_renderer.popColor();

				p_renderer.begin(PrimitiveModePoints);
				p_renderer.vertex(_origin, Gizmo::kHandleLinePixels * 2.0f, Color_Gold);
				p_renderer.end();
			}
		}
		else if (_id == p_renderer.getHotId())
		{
			color = Color_Gold;
		}
		aligned = std::max(Remap(aligned, 0.9f, 1.0f), 0.1f);
		if (p_renderer.getActiveId() == _id)
		{
			aligned = 1.0f;
		}
		p_renderer.pushColor(color);
		p_renderer.pushSize(Gizmo::kHandleLinePixels);
		//matrixStack.push_back(matrixStack.back() * LookAt(_origin, _origin + _axis));
		// pushMatrix(getMatrix() * LookAt(_origin, _origin + _axis, m_appData.m_worldUp));
		float sRad = fmod(p_drag.angle(), TwoPi);//p_drag.angle();
		float eRad = fmod(_angle, TwoPi);//_angle;
		p_renderer.drawPoint(Eigen::Vector3f(cosf(eRad) * _worldRadius, sinf(eRad) * _worldRadius, 0.0f),30);
		p_renderer.begin(PrimitiveModeLineLoop);
		const int detail = p_renderer.estimateLevelOfDetail(_origin, _worldRadius, 32, 128);
		auto x = p_drag.position().normalized();
		auto y=x.cross(_axis);

		if (sRad > eRad) {
			//std::swap(sRad,eRad);
		}
		
		for (int i = 0; i < detail; ++i)
		{
			float rad = -(eRad-sRad)* ((float)i / (float)detail);
			p_renderer.vertex(cosf(rad)* _worldRadius* x + sinf(rad) * _worldRadius * y);
			//vertex(Eigen::Vector3f(cosf(rad) * _worldRadius, sinf(rad) * _worldRadius, 0.0f));

			// post-modify the alpha for parts of the ring occluded by the sphere
			VertexFormat* vd = p_renderer.lastVertex();
			if (vd != nullptr)
			{
				Eigen::Vector3f v = Eigen::Vector3f(vd->positionSize.x(), vd->positionSize.y(), vd->positionSize.z());
				float d = (_origin - v).normalized().dot(cameraParam.viewDirectioin);
				d = std::max(_minAlpha, std::max(Remap(d, 0.1f, 0.2f), aligned));
				vd->color(0) = vd->color(0) * d;
			}
		}
		p_renderer.end();
		//matrixStack.pop_back();
		p_renderer.popSize();
		p_renderer.popColor();
	}
}
