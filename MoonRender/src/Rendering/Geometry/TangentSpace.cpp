#include "Rendering/Geometry/TangentSpace.h"

#include <cmath>
#include <cstring>

#include <Maths/FVector2.h>
#include <Maths/FVector3.h>

namespace
{
	uint32_t ReadIndex(const void*& p_pointer, uint32_t p_indexSize)
	{
		if (p_indexSize == sizeof(uint16_t))
		{
			const auto*& p = reinterpret_cast<const uint16_t*&>(p_pointer);
			return *p++;
		}
		const auto*& p = reinterpret_cast<const uint32_t*&>(p_pointer);
		return *p++;
	}

	/** Highest index in the range, i.e. the vertex count is that + 1. */
	uint32_t MaxIndex(const void* p_indexData, uint32_t p_indexSize, uint32_t p_indexStart, uint32_t p_indexCount)
	{
		const void* pointer = p_indexData;
		uint32_t maxIndex = 0;
		for (uint32_t i = 0; i < p_indexStart + p_indexCount; ++i)
		{
			const uint32_t index = ReadIndex(pointer, p_indexSize);
			if (i >= p_indexStart && index > maxIndex)
			{
				maxIndex = index;
			}
		}
		return maxIndex;
	}
}

void Rendering::Geometry::GenerateTangents(
	void* p_vertexData, uint32_t p_vertexSize, const void* p_indexData, uint32_t p_indexSize,
	uint32_t p_indexStart, uint32_t p_indexCount, uint32_t p_normalOffset,
	uint32_t p_texCoordOffset, uint32_t p_tangentOffset)
{
	// Tangent generation from http://www.terathon.com/code/tangent.html
	uint32_t minVertex = 0xFFFFFFFF;
	uint32_t maxVertex = 0;

	auto* vertices = static_cast<uint8_t*>(p_vertexData);

	const void* scan = p_indexData;
	for (uint32_t i = p_indexStart; i < p_indexStart + p_indexCount; ++i)
	{
		const uint32_t v = ReadIndex(scan, p_indexSize);
		if (v < minVertex) minVertex = v;
		if (v > maxVertex) maxVertex = v;
	}

	const uint32_t vertexCount = maxVertex + 1;
	auto* tan1 = new Maths::FVector3[vertexCount * 2];
	Maths::FVector3* tan2 = tan1 + vertexCount;
	std::memset(tan1, 0, sizeof(Maths::FVector3) * vertexCount * 2);

	const void* indexPointer = p_indexData;
	for (uint32_t i = p_indexStart; i + 2 < p_indexStart + p_indexCount; i += 3)
	{
		const uint32_t i1 = ReadIndex(indexPointer, p_indexSize);
		const uint32_t i2 = ReadIndex(indexPointer, p_indexSize);
		const uint32_t i3 = ReadIndex(indexPointer, p_indexSize);

		const auto& v1 = *reinterpret_cast<const Maths::FVector3*>(vertices + i1 * p_vertexSize);
		const auto& v2 = *reinterpret_cast<const Maths::FVector3*>(vertices + i2 * p_vertexSize);
		const auto& v3 = *reinterpret_cast<const Maths::FVector3*>(vertices + i3 * p_vertexSize);

		const auto& w1 = *reinterpret_cast<const Maths::FVector2*>(vertices + i1 * p_vertexSize + p_texCoordOffset);
		const auto& w2 = *reinterpret_cast<const Maths::FVector2*>(vertices + i2 * p_vertexSize + p_texCoordOffset);
		const auto& w3 = *reinterpret_cast<const Maths::FVector2*>(vertices + i3 * p_vertexSize + p_texCoordOffset);

		const float x1 = v2.x - v1.x, x2 = v3.x - v1.x;
		const float y1 = v2.y - v1.y, y2 = v3.y - v1.y;
		const float z1 = v2.z - v1.z, z2 = v3.z - v1.z;

		const float s1 = w2.x - w1.x, s2 = w3.x - w1.x;
		const float t1 = w2.y - w1.y, t2 = w3.y - w1.y;

		const float determinante = s1 * t2 - s2 * t1;
		const float r = std::abs(determinante) > 1.0e-8f ? 1.0f / determinante : 0.0f;
		const Maths::FVector3 sdir((t2 * x1 - t1 * x2) * r, (t2 * y1 - t1 * y2) * r, (t2 * z1 - t1 * z2) * r);
		const Maths::FVector3 tdir((s1 * x2 - s2 * x1) * r, (s1 * y2 - s2 * y1) * r, (s1 * z2 - s2 * z1) * r);

		tan1[i1] += sdir;
		tan1[i2] += sdir;
		tan1[i3] += sdir;

		tan2[i1] += tdir;
		tan2[i2] += tdir;
		tan2[i3] += tdir;
	}

	for (uint32_t i = minVertex; i <= maxVertex; ++i)
	{
		const auto& n = *reinterpret_cast<const Maths::FVector3*>(vertices + i * p_vertexSize + p_normalOffset);
		const Maths::FVector3& t = tan1[i];

		// Gram-Schmidt orthogonalise against the normal.
		Maths::FVector3 tangent = Maths::FVector3::Normalize(t - n * Maths::FVector3::Dot(n, t));
		if (!std::isfinite(tangent.x) || !std::isfinite(tangent.y) || !std::isfinite(tangent.z))
		{
			tangent = Maths::FVector3{ 1.0f, 0.0f, 0.0f };
		}

		auto& destination = *reinterpret_cast<Maths::FVector3*>(vertices + i * p_vertexSize + p_tangentOffset);
		destination = tangent;
	}

	delete[] tan1;
}

void Rendering::Geometry::GenerateNormals(
	void* p_vertexData, uint32_t p_vertexSize, const void* p_indexData, uint32_t p_indexSize,
	uint32_t p_indexStart, uint32_t p_indexCount, uint32_t p_positionOffset, uint32_t p_normalOffset)
{
	auto* vertices = static_cast<uint8_t*>(p_vertexData);
	const uint32_t vertexCount = MaxIndex(p_indexData, p_indexSize, p_indexStart, p_indexCount) + 1;

	auto* normals = new Maths::FVector3[vertexCount];
	for (uint32_t i = 0; i < vertexCount; ++i)
	{
		normals[i] = Maths::FVector3{ 0.0f, 0.0f, 0.0f };
	}

	const void* indexPointer = p_indexData;
	for (uint32_t i = p_indexStart; i + 2 < p_indexStart + p_indexCount; i += 3)
	{
		const uint32_t i1 = ReadIndex(indexPointer, p_indexSize);
		const uint32_t i2 = ReadIndex(indexPointer, p_indexSize);
		const uint32_t i3 = ReadIndex(indexPointer, p_indexSize);

		const auto& p1 = *reinterpret_cast<const Maths::FVector3*>(vertices + i1 * p_vertexSize + p_positionOffset);
		const auto& p2 = *reinterpret_cast<const Maths::FVector3*>(vertices + i2 * p_vertexSize + p_positionOffset);
		const auto& p3 = *reinterpret_cast<const Maths::FVector3*>(vertices + i3 * p_vertexSize + p_positionOffset);

		const Maths::FVector3 faceNormal = Maths::FVector3::Cross(p2 - p1, p3 - p1);
		normals[i1] += faceNormal;
		normals[i2] += faceNormal;
		normals[i3] += faceNormal;
	}

	for (uint32_t i = 0; i < vertexCount; ++i)
	{
		Maths::FVector3 normal = Maths::FVector3::Normalize(normals[i]);
		if (!std::isfinite(normal.x) || !std::isfinite(normal.y) || !std::isfinite(normal.z))
		{
			normal = Maths::FVector3{ 0.0f, 1.0f, 0.0f };
		}
		auto& destination = *reinterpret_cast<Maths::FVector3*>(vertices + i * p_vertexSize + p_normalOffset);
		destination = normal;
	}

	delete[] normals;
}
