#pragma once
#include <Eigen/Core>
#include "base/Tools2D.h"
#include "base/Vector3D.h"
namespace MOON {
	struct SketcherPlane2D
	{
		Base::Vector3d xAxis { 1.0,0.0,0.0 };
		Base::Vector3d yAxis { 0,1,0 };
		Base::Vector3d normal{ 0,0,1 };
		Base::Vector3d origin { 0,0,0 };
		Base::Vector3d value(const Base::Vector2d& coord) const {
			return origin + coord.x * xAxis + coord.y * yAxis;
		}
		Base::Vector3d value(double x, double y) const {
			return origin + x * xAxis + y * yAxis;
		}
		Eigen::Vector3f valueEigen(double x, double y) const {
			Base::Vector3d ret = origin + x * xAxis + y * yAxis;
			return Eigen::Vector3f(ret.x,ret.y,ret.z);
		}
		Eigen::Vector3f valueEigen(const Base::Vector2d& coord) const {
			Base::Vector3d ret = origin + coord.x * xAxis + coord.y * yAxis;
			return Eigen::Vector3f(ret.x, ret.y, ret.z);
		}
		SketcherPlane2D() = default;
	};
}
