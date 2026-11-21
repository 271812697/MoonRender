#pragma once
#include <Eigen/Eigen>
#include <Eigen/Core>
#include <Eigen/Dense>

namespace MOON
{
	struct Line
	{
		Eigen::Vector3f m_origin;
		Eigen::Vector3f m_direction; // unit length
		Line()
		{
		}
		Line(const Eigen::Vector3f& _origin, const Eigen::Vector3f& _direction)
			: m_origin(_origin)
			, m_direction(_direction)
		{
		}
	};
	struct Ray
	{
		Eigen::Vector3f m_origin;
		Eigen::Vector3f m_direction; // unit length

		Ray()
		{
		}
		Ray(const Eigen::Vector3f& _origin, const Eigen::Vector3f& _direction)
			: m_origin(_origin)
			, m_direction(_direction)
		{
		}
	};
	struct LineSegment
	{
		Eigen::Vector3f m_start;
		Eigen::Vector3f m_end;
		LineSegment()
		{
		}
		LineSegment(const Eigen::Vector3f& _start, const Eigen::Vector3f& _end)
			: m_start(_start)
			, m_end(_end)
		{
		}
	};
	struct Sphere
	{
		Eigen::Vector3f m_origin;
		float m_radius;

		Sphere()
		{
		}
		Sphere(const Eigen::Vector3f& _origin, float _radius)
			: m_origin(_origin)
			, m_radius(_radius)
		{
		}
	};
	struct Plane
	{
		Eigen::Vector3f m_normal;
		float m_offset;

		Plane()
		{
		}
		Plane(const Eigen::Vector3f& _normal, float _offset)
			: m_normal(_normal)
			, m_offset(_offset)
		{
		}
		Plane(const Eigen::Vector3f& _normal, const Eigen::Vector3f& _origin)
			: m_normal(_normal)
			, m_offset(_normal.dot(_origin))
		{
		}
	};
	struct Capsule
	{
		Eigen::Vector3f m_start;
		Eigen::Vector3f m_end;
		float m_radius;

		Capsule()
		{
		}
		Capsule(const Eigen::Vector3f& _start, const Eigen::Vector3f& _end, float _radius)
			: m_start(_start)
			, m_end(_end)
			, m_radius(_radius)
		{
		}
	};

	// Ray-primitive intersections. Use Intersects() when you don't need t.
	bool Intersects(const Ray& _ray, const Plane& _plane);
	bool Intersect(const Ray& _ray, const Plane& _plane, float& t0_);
	bool Intersects(const Ray& _ray, const Sphere& _sphere);
	bool Intersect(const Ray& _ray, const Sphere& _sphere, float& t0_, float& t1_);
	bool Intersects(const Ray& _ray, const Capsule& _capsule);
	bool Intersect(const Ray& _ray, const Capsule& _capsule, float& t0_, float& t1_);

	bool Intersect(const Ray& ray, const Eigen::Vector3f& _a, const Eigen::Vector3f& _b,
		const Eigen::Vector3f& _c, float& tr);
	// the intersect point between palne and segment
	bool Intersect(const Plane& plane, const Eigen::Vector3f& v0, const Eigen::Vector3f& v1, Eigen::Vector3f& res);

	std::vector<Eigen::Vector3f> clipBox(const Plane& plane, const Eigen::Vector3f& _min, const Eigen::Vector3f& _max);
	//Maybe not fully considered
	bool IntersectBox(const Ray& ray, const Eigen::Vector3f& _min, const Eigen::Vector3f& _max, float& tr, int* faceIndex = nullptr);
	bool IntersectWithCone(const Ray& ray, const Eigen::Vector3f& _origin, const Eigen::Vector3f& _normal, float height, float _radius, float& tr);


	// Find point t0_ along _line0 nearest to _line1 and t1_ along _line1 nearest to _line0.
	void Nearest(const Line& _line0, const Line& _line1, float& t0_, float& t1_);
	// Find point tr_ along _ray nearest to _line and tl_ along _line nearest to _ray.
	void Nearest(const Ray& _ray, const Line& _line, float& tr_, float& tl_);

	Eigen::Vector3f Nearest(const Ray& _ray, const LineSegment& _segment, float& tr_);

	float Distance2(const Ray& _ray, const LineSegment& _segment);

	inline float Distance(const Eigen::Vector4<float>& _plane, const Eigen::Vector3f& _point)
	{
		return _plane.x() * _point.x() + _plane.y() * _point.y() + _plane.z() * _point.z() - _plane.w();
	}

	constexpr float Pi = 3.14159265359f;
	constexpr float TwoPi = 2.0f * Pi;
	constexpr float HalfPi = 0.5f * Pi;
	Eigen::Vector3f Radians(const Eigen::Vector3f& degrees);
	float Radians(float _degrees);
	float Degrees(float _radians);
	Eigen::Matrix4f LookAt(const Eigen::Vector3f& _from, const Eigen::Vector3f& _to, const Eigen::Vector3f& _up = Eigen::Vector3f(0.0f, 1.0f, 0.0f));
	Eigen::Matrix4f Coord3(const Eigen::Vector3f& _translation, const Eigen::Matrix3f& _rotation, const Eigen::Vector3f& _scale);
	Eigen::Vector3f MatrixMulPoint(const Eigen::Matrix4f& matrix, const Eigen::Vector3f& point);
	Eigen::Vector3f MatrixMulDir(const Eigen::Matrix4f& matrix, const Eigen::Vector3f& dir);
	Eigen::Matrix4f AlignZ(const Eigen::Vector3f& _axis, const Eigen::Vector3f& _up = Eigen::Vector3f(0.0f, 1.0f, 0.0f));
	Eigen::Matrix4f LookAt(const Eigen::Vector3f& _from, const Eigen::Vector3f& _to, const Eigen::Vector3f& _up);
	Eigen::Matrix3f RotationMatrix(const Eigen::Vector3f& axis, float rads);
	Eigen::Matrix3f RotationMatrix(const Eigen::Vector3f& x, const Eigen::Vector3f& y, const Eigen::Vector3f& z );
	Eigen::Matrix3f RotationMatrixX(const Eigen::Vector3f& axis);
	Eigen::Matrix3f RotationMatrixZ(const Eigen::Vector3f& axis);
	Eigen::Matrix4f RotationMatrix(const Eigen::Vector3f& point, const Eigen::Vector3f& axis, float rads);
	Eigen::Matrix3f EulerXYZToMatrix(const Eigen::Vector3f& angles);
	Eigen::Matrix4f EulerXYZToMatrix4(const Eigen::Vector3f& angles);
	Eigen::Matrix4f EulerXYZToMatrix4Degree(const Eigen::Vector3f& angles);
	Eigen::Vector3f ToEulerXYZ(const Eigen::Matrix3f& _m);
	Eigen::Vector4f EulerXYZToQuat(float x, float y, float z);
	Eigen::Matrix4f ReflectionPlane(const Eigen::Vector3f& normal, float offset);

	Eigen::Vector3f Snap(const Eigen::Vector3f& _val, float _snap);
	float Snap(float _val, float _snap);
	Eigen::Vector3f Snap(const Eigen::Vector3f& _pos, const Plane& _plane, float _snap);
	template <typename T>
	inline T Clamp(T _a, T _min, T _max)
	{
		return std::max(std::min(_a, _max), _min);
	}
	inline float Remap(float _x, float _start, float _end)
	{
		return Clamp(_x * (1.0f / (_end - _start)) + (-_start / (_end - _start)), 0.0f, 1.0f);
	}
}
