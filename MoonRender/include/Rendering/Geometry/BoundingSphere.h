#pragma once
#include <Maths/FVector3.h>

namespace Rendering::Geometry
{
	/**
	* Data structure that defines a bounding sphere (Position + radius)
	*/
	struct BoundingSphere
	{
		/** A sphere that was never computed is *empty* - at the origin, with no
		 * radius - rather than whatever the memory happened to hold.
		 *
		 * Its bounds are computed by whoever builds the mesh, and not always on the
		 * same thread or in the same frame (a topology batch computes its line mesh
		 * on a job thread), so consumers read this before it is filled in. An
		 * uninitialised radius there is not "nothing to frame" but a non-finite
		 * camera target. */
		Maths::FVector3 position{ 0.0f, 0.0f, 0.0f };
		float radius = 0.0f;
		void merge(const BoundingSphere& other)
		{
			Maths::FVector3 centerDir = other.position - position;
			float distance = Maths::FVector3::Length(centerDir);
			// If one sphere is completely inside the other, take the larger one
			if (distance + other.radius <= radius)
			{
				return;
			}
			if (distance + radius <= other.radius)
			{
				position = other.position;
				radius = other.radius;
				return;
			}
			// Otherwise, calculate the new sphere that encompasses both
			float newRadius = (distance + radius + other.radius) * 0.5f;
			Maths::FVector3 newCenter = position;
			if (distance > 0.0f)
			{
				newCenter += centerDir * ((newRadius - radius) / distance);
			}
			position = newCenter;
			radius = newRadius;
		}
	};
}
