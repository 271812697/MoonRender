#include <Maths/MathUtil.h>

namespace MOON
{
	Eigen::Matrix4f Coord3(
		const Eigen::Vector3f& _translation, const Eigen::Matrix3f& _rotation, const Eigen::Vector3f& _scale)
	{
		Eigen::Matrix4f ret;
		ret.col(0) << _rotation(0, 0) * _scale.x(), _rotation(1, 0)* _scale.x(), _rotation(2, 0)* _scale.x(), 0.0;
		ret.col(1) << _rotation(0, 1) * _scale.y(), _rotation(1, 1)* _scale.y(), _rotation(2, 1)* _scale.y(), 0.0;
		ret.col(2) << _rotation(0, 2) * _scale.z(), _rotation(1, 2)* _scale.z(), _rotation(2, 2)* _scale.z(), 0.0;
		ret.col(3) << _translation.x(), _translation.y(), _translation.z(), 1.0;
		return ret;
	}
	Eigen::Vector3f MatrixMulPoint(const Eigen::Matrix4f& matrix, const Eigen::Vector3f& point)
	{
		Eigen::Vector4<float> homeous = matrix * Eigen::Vector4<float>(point.x(), point.y(), point.z(), 1.0);
		return Eigen::Vector3f(homeous.x() / homeous.w(), homeous.y() / homeous.w(), homeous.z() / homeous.w());
	}
	Eigen::Vector3f MatrixMulDir(const Eigen::Matrix4f& matrix, const Eigen::Vector3f& dir)
	{
		Eigen::Vector4<float> res = matrix * Eigen::Vector4<float>(dir.x(), dir.y(), dir.z(), 0.0);
		return res.head<3>().normalized();
		//return Eigen::Vector3f(homeous.x() / homeous.w(), homeous.y() / homeous.w(), homeous.z() / homeous.w());
	}
	bool Intersects(const Ray& _ray, const Plane& _plane)
	{
		float x = _plane.m_normal.dot(_ray.m_direction);
		return x <= 0.0f;
	}
	bool Intersect(const Ray& _ray, const Plane& _plane, float& t0_)
	{
		t0_ = _plane.m_normal.dot((_plane.m_normal * _plane.m_offset) - _ray.m_origin) /
			_plane.m_normal.dot(_ray.m_direction);
		return t0_ >= 0.0f;
	}
	bool Intersects(const Ray& _r, const Sphere& _s)
	{
		Eigen::Vector3f p = _s.m_origin - _r.m_origin;
		float p2 = p.squaredNorm();
		float q = p.dot(_r.m_direction);
		float r2 = _s.m_radius * _s.m_radius;
		if (q < 0.0f && p2 > r2)
		{
			return false;
		}

		return p2 - (q * q) <= r2;
	}
	bool Intersect(const Ray& _r, const Sphere& _s, float& t0_, float& t1_)
	{
		Eigen::Vector3f p = _s.m_origin - _r.m_origin;
		float q = p.dot(_r.m_direction);
		if (q < 0.0f)
		{
			return false;
		}

		float p2 = p.squaredNorm() - q * q;
		float r2 = _s.m_radius * _s.m_radius;
		if (p2 > r2)
		{
			return false;
		}

		float s = sqrtf(r2 - p2);
		t0_ = std::max(q - s, 0.0f);
		t1_ = q + s;

		return true;
	}
	bool Intersects(const Ray& _ray, const Capsule& _capsule)
	{
		return Distance2(_ray, LineSegment(_capsule.m_start, _capsule.m_end)) < _capsule.m_radius * _capsule.m_radius;
	}
	bool Intersect(const Ray& _ray, const Capsule& _capsule, float& t0_, float& t1_)
	{
		// \todo implement
		t0_ = t1_ = 0.0f;
		return Intersects(_ray, _capsule);
	}

	void Nearest(const Line& _line0, const Line& _line1, float& t0_, float& t1_)
	{
		Eigen::Vector3f p = _line0.m_origin - _line1.m_origin;
		float q = _line0.m_direction.dot(_line1.m_direction);
		float s = _line1.m_direction.dot(p);

		float d = 1.0f - q * q;
		if (d < FLT_EPSILON) // lines are parallel
		{
			t0_ = 0.0f;
			t1_ = s;
		}
		else
		{
			float r = _line0.m_direction.dot(p);
			t0_ = (q * s - r) / d;
			t1_ = (s - q * r) / d;
		}
	}

	bool Intersect(const Ray& ray, const Eigen::Vector3f& _a, const Eigen::Vector3f& _b,
		const Eigen::Vector3f& _c, float& tr)
	{

		const Eigen::Vector3f edge1 = _b - _a;
		const Eigen::Vector3f edge2 = _c - _a;
		const Eigen::Vector3f h = ray.m_direction.cross(edge2);
		const float a = edge1.dot(h);

		// 射线与三角形平行或共面（无交点）
		if (std::fabs(a) < 1e-6f) {
			return false;
		}

		const float f = 1.0f / a;
		const Eigen::Vector3f s = ray.m_origin - _a;
		const float u = f * s.dot(h);

		// u不在[0,1]范围内，交点在三角形外
		if (u < 0.0f || u > 1.0f) {
			return false;
		}

		const Eigen::Vector3f q = s.cross(edge1);
		const float v = f * ray.m_direction.dot(q);

		// v不在[0,1]或u+v>1，交点在三角形外
		if (v < 0.0f || u + v > 1.0f) {
			return false;
		}

		// 计算射线参数t（距离）
		const float t = f * edge2.dot(q);

		// t>0表示交点在射线方向上（有效距离）
		if (t > 1e-6f) {
			tr = t; // 传出距离
			return true;
		}

		// 交点在射线反方向（无效）
		return false;
	}
	bool Intersect(const Plane& plane, const Eigen::Vector3f& v0, const Eigen::Vector3f& v1, Eigen::Vector3f& res)
	{
		if (v0.isApprox(v1))
		{
			return false;
		}

		Eigen::Vector3f dir = (v1 - v0).normalized();
		float len = (v1 - v0).norm();
		Ray ray(v0, dir);
		float tr = -1;
		if (Intersect(ray, plane, tr))
		{
			if (tr <= len)
			{
				res = ray.m_origin + ray.m_direction * tr;
				return true;
			}
		}
		return false;
	}
	std::vector< Eigen::Vector3f> clipBox(const Plane& plane, const Eigen::Vector3f& min, const Eigen::Vector3f& max)
	{
		Eigen::Vector3f points[8] = { min, Eigen::Vector3f(max.x(), min.y(), min.z()),
			Eigen::Vector3f(max.x(), min.y(), max.z()), Eigen::Vector3f(min.x(), min.y(), max.z()),
			Eigen::Vector3f(min.x(), max.y(), min.z()), Eigen::Vector3f(max.x(), max.y(), min.z()), max,
			Eigen::Vector3f(min.x(), max.y(), max.z()) };
		struct PIndex
		{
			int v0, v1, v2, v3;
		};
		std::vector<Eigen::Vector3f> ans;
		//get six planes
		PIndex ps[6] = { {0, 1, 2, 3}, {1, 2, 6, 5}, {3, 2, 6, 7}, {0, 1, 5, 4}, {4, 5, 6, 7}, {0, 3, 7, 4} };
		for (int i = 0; i < 6; i++)
		{
			// Intersect(const Plane& plane, const Eigen::Vector3f& v0, const Eigen::Vector3f& v1, Eigen::Vector3f& res)
			Eigen::Vector3f res(0, 0, 0);
			if (Intersect(plane, points[ps[i].v0], points[ps[i].v1], res))
			{
				ans.push_back(res);
			}
			if (Intersect(plane, points[ps[i].v1], points[ps[i].v2], res))
			{
				ans.push_back(res);
			}
			if (Intersect(plane, points[ps[i].v2], points[ps[i].v3], res))
			{
				ans.push_back(res);
			}
			if (Intersect(plane, points[ps[i].v0], points[ps[i].v3], res))
			{
				ans.push_back(res);
			}
		}
		return ans;
	}
	bool IntersectBox(const Ray& ray, const Eigen::Vector3f& _min, const Eigen::Vector3f& _max, float& tr, int* faceIndex)
	{
		Eigen::Vector3f direction = ray.m_direction;
		Eigen::Vector3f a = _min - ray.m_origin;
		Eigen::Vector3f b = _max - ray.m_origin;
		bool calX = true, calY = true, calZ = true;
		if (abs(direction.x()) < FLT_EPSILON)
		{
			direction.x() = FLT_EPSILON;
			//calX = false;
		}
		if (abs(direction.y()) < FLT_EPSILON)
		{
			direction.y() = FLT_EPSILON;
			//calY = false;
		}
		if (abs(direction.z()) < FLT_EPSILON)
		{
			direction.z() = FLT_EPSILON;
			//calZ = false;
		}
		if (calX)
		{
			a.x() = a.x() / direction.x();
			b.x() = b.x() / direction.x();
		}
		if (calY)
		{
			a.y() = a.y() / direction.y();
			b.y() = b.y() / direction.y();
		}
		if (calZ)
		{
			a.z() = a.z() / direction.z();
			b.z() = b.z() / direction.z();
		}

		if (calX && calY && calZ)
		{
			Eigen::Vector3f c(std::min(a.x(), b.x()), std::min(a.y(), b.y()), std::min(a.z(), b.z()));
			Eigen::Vector3f d(std::max(a.x(), b.x()), std::max(a.y(), b.y()), std::max(a.z(), b.z()));

			float e = std::fmax(c.z(), std::fmax(c.x(), c.y()));
			float f = std::fmin(d.z(), std::fmin(d.x(), d.y()));
			int index = -1;
			if (c.x() >= c.y() && c.x() >= c.z())
			{
				index = 1;
				if (direction.x() <= 0)
				{
					index = 0;

				}
			}
			else if (c.y() >= c.x() && c.y() >= c.z())
			{
				index = 3;
				if (direction.y() <= 0)
				{
					index = 2;
				}

			}
			else
			{
				index = 5;
				if (direction.z() <= 0)
				{
					index = 4;
				}
			}


			bool ret = (e <= f);
			if (ret)
			{
				if (faceIndex)
				{
					*faceIndex = index;
				}
				tr = e;
			}
			return ret;
		}
		else if (calX && calY)
		{
			Eigen::Vector3f c(std::min(a.x(), b.x()), std::min(a.y(), b.y()), std::min(a.z(), b.z()));
			Eigen::Vector3f d(std::max(a.x(), b.x()), std::max(a.y(), b.y()), std::max(a.z(), b.z()));
			float e = std::fmax(c.x(), c.y());
			float f = std::fmin(d.x(), d.y());
			int index = -1;
			if (c.x() >= c.y())
			{
				index = 1;
				if (direction.x() <= 0)
				{
					index = 0;
				}
			}
			else
			{
				index = 3;
				if (direction.y() <= 0)
				{
					index = 2;
				}
			}


			bool ret = (e <= f);
			if (ret)
			{
				if (faceIndex)
				{
					*faceIndex = index;
				}
				tr = e;
			}
			return ret;
		}
		else if (calX && calZ)
		{
			Eigen::Vector3f c(std::min(a.x(), b.x()), std::min(a.y(), b.y()), std::min(a.z(), b.z()));
			Eigen::Vector3f d(std::max(a.x(), b.x()), std::max(a.y(), b.y()), std::max(a.z(), b.z()));
			float e = std::fmax(c.z(), c.x());
			float f = std::fmin(d.z(), d.x());
			int index = -1;
			if (c.x() >= c.z())
			{
				index = 1;
				if (direction.x() <= 0)
				{
					index = 0;
				}
			}

			else
			{
				index = 5;
				if (direction.z() <= 0)
				{
					index = 4;
				}
			}

			bool ret = (e <= f);
			if (ret)
			{
				if (faceIndex)
				{
					*faceIndex = index;
				}
				tr = e;
			}
			return ret;
		}
		else if (calY && calZ)
		{
			Eigen::Vector3f c(std::min(a.x(), b.x()), std::min(a.y(), b.y()), std::min(a.z(), b.z()));
			Eigen::Vector3f d(std::max(a.x(), b.x()), std::max(a.y(), b.y()), std::max(a.z(), b.z()));

			float e = std::fmax(c.z(), c.y());
			float f = std::fmin(d.z(), d.y());
			int index = -1;
			if (c.y() >= c.z())
			{
				index = 3;
				if (direction.y() <= 0)
				{
					index = 2;
				}
			}
			else
			{
				index = 5;
				if (direction.z() <= 0)
				{
					index = 4;
				}
			}

			bool ret = (e <= f);
			if (ret)
			{
				if (faceIndex)
				{
					*faceIndex = index;
				}
				tr = e;
			}
			return ret;
		}
		else if (calX)
		{
			Eigen::Vector3f c(std::min(a.x(), b.x()), std::min(a.y(), b.y()), std::min(a.z(), b.z()));
			Eigen::Vector3f d(std::max(a.x(), b.x()), std::max(a.y(), b.y()), std::max(a.z(), b.z()));

			float e = c.x();
			float f = d.x();
			int index = -1;
			index = 1;
			if (direction.x() <= 0)
			{
				index = 0;
			}

			bool ret = (e <= f);
			if (ret)
			{
				if (faceIndex)
				{
					*faceIndex = index;
				}
				tr = e;
			}
			return ret;
		}
		else if (calY)
		{
			Eigen::Vector3f c(std::min(a.x(), b.x()), std::min(a.y(), b.y()), std::min(a.z(), b.z()));
			Eigen::Vector3f d(std::max(a.x(), b.x()), std::max(a.y(), b.y()), std::max(a.z(), b.z()));

			float e = c.y();
			float f = d.y();
			int index = -1;
			index = 3;
			if (direction.x() <= 0)
			{
				index = 2;
			}

			bool ret = (e <= f);
			if (ret)
			{
				if (faceIndex)
				{
					*faceIndex = index;
				}
				tr = e;
			}
			return ret;
		}
		else
		{
			Eigen::Vector3f c(std::min(a.x(), b.x()), std::min(a.y(), b.y()), std::min(a.z(), b.z()));
			Eigen::Vector3f d(std::max(a.x(), b.x()), std::max(a.y(), b.y()), std::max(a.z(), b.z()));

			float e = c.z();
			float f = d.z();
			int index = -1;
			index = 5;
			if (direction.x() <= 0)
			{
				index = 4;
			}

			bool ret = (e <= f);
			if (ret)
			{
				if (faceIndex)
				{
					*faceIndex = index;
				}
				tr = e;
			}
			return ret;
		}
		return false;
	}
	bool IntersectWithCone(const Ray& ray, const Eigen::Vector3f& _origin, const Eigen::Vector3f& _normal,
		float height, float _radius, float& tr)
	{
		bool ret = false;
		float dis = FLT_MAX;
		auto transMat = LookAt(_origin, _origin + _normal);
		float cp = _radius;
		float sp = 0.0f;
		for (int i = 1; i <= 20; ++i)
		{

			Eigen::Vector3f v0 = MatrixMulPoint(transMat, Eigen::Vector3f(0, 0, 1) * height);
			Eigen::Vector3f v1 = MatrixMulPoint(transMat, Eigen::Vector3f(cp, sp, 0.0));

			float rad = TwoPi * ((float)i / 20.0);
			float c = cosf(rad) * _radius;
			float s = sinf(rad) * _radius;
			Eigen::Vector3f v2 = MatrixMulPoint(transMat, Eigen::Vector3f(c, s, 0.0));

			cp = c;
			sp = s;
			float tempDis = FLT_MAX;
			if (Intersect(ray, v0, v1, v2, tempDis))
			{
				ret = true;
				dis = std::fmin(dis, tempDis);
			}
		}
		Plane circle(_normal, _origin);
		float tempDis = FLT_MAX;
		if (Intersect(ray, circle, tempDis))
		{
			Eigen::Vector3f offset = (ray.m_origin + ray.m_direction * tempDis) - _origin;
			if (offset.norm() <= _radius)
			{
				ret = true;
				dis = std::fmin(dis, tempDis);
			}
		}
		if (ret)
		{
			tr = dis;
		}
		return ret;
	}

	void Nearest(const Ray& _ray, const Line& _line, float& tr_, float& tl_)
	{
		Nearest(Line(_ray.m_origin, _ray.m_direction), _line, tr_, tl_);
		tr_ = std::max(tr_, 0.0f);
	}
	Eigen::Vector3f Nearest(const Ray& _ray, const LineSegment& _segment, float& tr_)
	{
		Eigen::Vector3f ldir = _segment.m_end - _segment.m_start;
		Eigen::Vector3f p = _segment.m_start - _ray.m_origin;
		float q = ldir.squaredNorm();
		float r = ldir.dot(_ray.m_direction);
		float s = ldir.dot(p);
		float t = _ray.m_direction.dot(p);

		float sn, sd, tn, td;
		float denom = q - r * r;
		if (denom < FLT_EPSILON)
		{
			sd = td = 1.0f;
			sn = 0.0f;
			tn = t;
		}
		else
		{
			sd = td = denom;
			sn = r * t - s;
			tn = q * t - r * s;
			if (sn < 0.0f)
			{
				sn = 0.0f;
				tn = t;
				td = 1.0f;
			}
			else if (sn > sd)
			{
				sn = sd;
				tn = t + r;
				td = 1.0f;
			}
		}

		float ts;
		if (tn < 0.0f)
		{
			tr_ = 0.0f;
			if (r >= 0.0f)
			{
				ts = 0.0f;
			}
			else if (s <= q)
			{
				ts = 1.0f;
			}
			else
			{
				ts = -s / q;
			}
		}
		else
		{
			tr_ = tn / td;
			ts = sn / sd;
		}

		return _segment.m_start + ldir * ts;
	}
	float Distance2(const Ray& _ray, const LineSegment& _segment)
	{
		float tr;
		Eigen::Vector3f p = Nearest(_ray, _segment, tr);
		return (_ray.m_origin + _ray.m_direction * tr - p).squaredNorm();
	}

	Eigen::Matrix4f AlignZ(const Eigen::Vector3f& _axis, const Eigen::Vector3f& _up)
	{
		Eigen::Vector3f x, y;
		y = _up - _axis * _up.dot(_axis);
		float ylen = y.norm();
		if (ylen < FLT_EPSILON)
		{
			Eigen::Vector3f k = Eigen::Vector3f(1.0f, 0.0f, 0.0f);
			y = k - _axis * k.dot(_axis);
			ylen = y.norm();
			if (ylen < FLT_EPSILON)
			{
				k = Eigen::Vector3f(0.0f, 0.0f, 1.0f);
				y = k - _axis * k.dot(_axis);
				ylen = y.norm();
			}
		}
		y = y / ylen;
		x = y.cross(_axis);
		Eigen::Matrix4f ret;
		ret.row(0) << x.x(), y.x(), _axis.x(), 0.0;
		ret.row(1) << x.y(), y.y(), _axis.y(), 0.0;
		ret.row(2) << x.z(), y.z(), _axis.z(), 0.0;
		ret.row(3) << 0.0, 0.0, 0.0, 1.0;
		return ret;
	}
	Eigen::Matrix4f LookAt(
		const Eigen::Vector3f& _from, const Eigen::Vector3f& _to, const Eigen::Vector3f& _up)
	{
		Eigen::Matrix4f ret = AlignZ((_to - _from).normalized(), _up);
		ret(0, 3) = _from.x(); // inject translation
		ret(1, 3) = _from.y();
		ret(2, 3) = _from.z();
		ret(3, 3) = 1.0f;
		return ret;
	}
	Eigen::Matrix3f RotationMatrix(const Eigen::Vector3f& _axis, float _rads)
	{
		float c = cosf(_rads);
		float rc = 1.0f - c;
		float s = sinf(_rads);
		Eigen::Matrix3f ret;
		ret.row(0) << _axis.x() * _axis.x() + (1.0f - _axis.x() * _axis.x()) * c,
			_axis.x()* _axis.y()* rc - _axis.z() * s, _axis.x()* _axis.z()* rc + _axis.y() * s;
		ret.row(1) << _axis.x() * _axis.y() * rc + _axis.z() * s,
			_axis.y()* _axis.y() + (1.0f - _axis.y() * _axis.y()) * c, _axis.y()* _axis.z()* rc - _axis.x() * s;
		ret.row(2) << _axis.x() * _axis.z() * rc - _axis.y() * s, _axis.y()* _axis.z()* rc + _axis.x() * s,
			_axis.z()* _axis.z() + (1.0f - _axis.z() * _axis.z()) * c;
		return ret;
	}
	Eigen::Matrix3f RotationMatrix(const Eigen::Vector3f& x, const Eigen::Vector3f& y, const Eigen::Vector3f& z)
	{
		Eigen::Matrix3f ret;
		ret.block(0, 0, 3, 1) = x.normalized();
		ret.block(0, 1, 3, 1) = y.normalized();
		ret.block(0, 2, 3, 1) = z.normalized();
		return ret;
	}
	Eigen::Matrix3f RotationMatrixX(const Eigen::Vector3f& axis)
	{
		const Eigen::Vector3f r = { 1, 0, 0 };
		Eigen::Vector3f worldRight = axis.isApprox(r) ? Eigen::Vector3f(0, 1, 0) : r;
		Eigen::Vector3f up = axis.cross(worldRight).normalized();
		Eigen::Vector3f forward = axis.cross(up).normalized();
		Eigen::Matrix3f ret;
		ret.block(0, 0, 3, 1) = axis.normalized();
		ret.block(0, 1, 3, 1) = up;
		ret.block(0, 2, 3, 1) = forward;
		return ret;
	}
	Eigen::Matrix3f RotationMatrixZ(const Eigen::Vector3f& axis)
	{
		const Eigen::Vector3f r = { 1, 0, 0 };
		Eigen::Vector3f worldRight = axis.isApprox(r) ? Eigen::Vector3f(0, 1, 0) : r;
		Eigen::Vector3f up = axis.cross(worldRight).normalized();
		worldRight = up.cross(axis).normalized();
		Eigen::Matrix3f ret;
		ret.block(0, 0, 3, 1) = worldRight;
		ret.block(0, 1, 3, 1) = up;
		ret.block(0, 2, 3, 1) = axis.normalized();
		return ret;
	}
	Eigen::Matrix4f RotationMatrix(const Eigen::Vector3f& point, const Eigen::Vector3f& axis, float rads)
	{
		Eigen::Matrix4f t1 = Eigen::Matrix4f::Identity(), t2 = Eigen::Matrix4f::Identity(), t3 = Eigen::Matrix4f::Identity();
		t1.block(0, 3, 3, 1) = -point;
		t2.block(0, 3, 3, 1) = point;
		t3.block(0, 0, 3, 3) = RotationMatrix(axis, rads);
		return t2 * t3 * t1;
	}
	Eigen::Matrix3f EulerXYZToMatrix(const Eigen::Vector3f& angles)
	{
		float cx = cos(angles.x());
		float sx = sin(angles.x());
		float cy = cos(angles.y());
		float sy = sin(angles.y());
		float cz = cos(angles.z());
		float sz = sin(angles.z());
		Eigen::Matrix3f ret;
		ret.row(0) << cy * cz, sx* sy* cz - cx * sz, cx* sy* cz + sx * sz;
		ret.row(1) << cy * sz, sx* sy* sz + cx * cz, cx* sy* sz - sx * cz;
		ret.row(2) << -sy, sx* cy, cx* cy;
		return ret;
	}
	Eigen::Matrix4f EulerXYZToMatrix4(const Eigen::Vector3f& angles)
	{
		Eigen::Matrix4f ret = Eigen::Matrix4f::Identity();
		ret.block(0, 0, 3, 3) = EulerXYZToMatrix(angles);
		return ret;
	}
	Eigen::Matrix4f EulerXYZToMatrix4Degree(const Eigen::Vector3f& angles)
	{

		Eigen::Matrix4f ret = Eigen::Matrix4f::Identity();
		ret.block(0, 0, 3, 3) = EulerXYZToMatrix(Radians(angles));
		return ret;
	}
	Eigen::Vector3f Radians(const Eigen::Vector3f& degrees)
	{
		return Eigen::Vector3f(Radians(degrees.x()), Radians(degrees.y()), Radians(degrees.z()));
	}
	float Radians(float _degrees)
	{
		return _degrees * (Pi / 180.0f);
	}
	float Degrees(float _radians)
	{
		return _radians * (180.0f / Pi);
	}
	Eigen::Vector3f ToEulerXYZ(const Eigen::Matrix3f& _m)
	{

		Eigen::Vector3f ret;
		if (fabs(_m(2, 0)) < 1.0f)
		{
			ret(1) = -asinf(_m(2, 0));
			float c = 1.0f / cosf(ret.y());
			ret(0) = atan2f(_m(2, 1) * c, _m(2, 2) * c);
			ret(2) = atan2f(_m(1, 0) * c, _m(0, 0) * c);
		}
		else
		{
			ret(2) = 0.0f;
			if (!(_m(2, 0) > -1.0f))
			{
				ret(0) = ret(2) + atan2f(_m(0, 1), _m(0, 2));
				ret(1) = HalfPi;
			}
			else
			{
				ret(0) = -ret(2) + atan2f(-_m(0, 1), -_m(0, 2));
				ret(1) = -HalfPi;
			}
		}
		return ret;
	}

	Eigen::Vector4f EulerXYZToQuat(float x, float y, float z)
	{

		x = Radians(x) / 2;
		y = Radians(y) / 2;
		z = Radians(z) / 2;
		float sinX = sinf(x);
		float cosX = cosf(x);
		float sinY = sinf(y);
		float cosY = cosf(y);
		float sinZ = sinf(z);
		float cosZ = cosf(z);


		return Eigen::Vector4f(cosX * sinY * cosZ + sinX * cosY * sinZ, sinX * cosY * cosZ - cosX * sinY * sinZ,
			cosX * cosY * sinZ - sinX * sinY * cosZ, cosY * cosX * cosZ + sinY * sinX * sinZ);
	}

	Eigen::Matrix4f ReflectionPlane(const Eigen::Vector3f& normal, float d)
	{
		float nx = normal.x();
		float ny = normal.y();
		float nz = normal.z();
		Eigen::Matrix4f ret;
		ret.row(0) << 1 - 2 * nx * nx, -2 * ny * nx, -2 * nz * nx, -2 * d * nx;
		ret.row(1) << -2 * nx * ny, 1 - 2 * ny * ny, -2 * nz * ny, -2 * d * ny;
		ret.row(2) << -2 * nx * nz, -2 * ny * nz, 1 - 2 * nz * nz, -2 * d * nz;
		ret.row(3) << 0, 0, 0, 1;

		return ret;
	}

	Eigen::Vector3f Snap(const Eigen::Vector3f& _val, float _snap)
	{
		if (_snap > 0.0f)
		{
			return Eigen::Vector3f(
				floorf(_val.x() / _snap) * _snap, floorf(_val.y() / _snap) * _snap, floorf(_val.z() / _snap) * _snap);
		}
		return _val;
	}
	float Snap(float _val, float _snap)
	{
		if (_snap > 0.0f)
		{
			return floorf(_val / _snap) * _snap;
		}
		return _val;
	}
	Eigen::Vector3f Snap(const Eigen::Vector3f& _pos, const Plane& _plane, float _snap)
	{
		if (_snap > 0.0f)
		{
			// get basis vectors on the plane
			Eigen::Matrix3f basis = AlignZ(_plane.m_normal).block(0, 0, 3, 3);
			Eigen::Vector3f i = basis.col(0);
			Eigen::Vector3f j = basis.col(1);

			// decompose _pos in terms of the basis vectors
			i = i * _pos.dot(i);
			j = j * _pos.dot(j);

			// snap the vector lengths
			float ilen = i.norm();
			float jlen = j.norm();

			if (ilen < 1e-7f || jlen < 1e-7f) // \hack prevent DBZ when _pos is 0
			{
				return _pos;
			}

			i = i / ilen;
			ilen = floorf(ilen / _snap) * _snap;
			i = i * ilen;
			j = j / jlen;
			jlen = floorf(jlen / _snap) * _snap;
			j = j * jlen;

			return i + j;
		}
		return _pos;
	}
}
