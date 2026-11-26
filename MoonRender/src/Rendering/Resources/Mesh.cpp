#include <array>
#include <memory>
#include <vector>
#include <glad/glad.h>
#include <Rendering/Backend/BackendResources.h>
#include <Rendering/Backend/HandleAllocator.h>
#include <Rendering/Resources/Mesh.h>
#include "Rendering/Geometry/split_bvh.h"

Rendering::Resources::Mesh::Mesh(
	const std::vector<Geometry::Vertex>& p_vertices,
	const std::vector< uint32_t>& p_indices,
	uint32_t p_materialIndex,
	Settings::EPrimitiveMode primitiveMode
) : mPrimitiveMode(primitiveMode),
	m_vertexCount(static_cast<uint32_t>(p_vertices.size())),
	m_indicesCount(static_cast<uint32_t>(p_indices.size()))
{
	isIndex = m_indicesCount> 0;
	
	m_indices.resize(m_indicesCount);
	m_vertices.resize(m_vertexCount);
	uploadIndicesCount.resize(1);
	CreateResources();

	memcpy(m_indices.data(), p_indices.data(), p_indices.size()*sizeof(uint32_t));
	for (int i = 0;i < p_vertices.size();i++) {
	
		m_vertices[i].position.x = p_vertices[i].position[0];
		m_vertices[i].position.y = p_vertices[i].position[1];
		m_vertices[i].position.z = p_vertices[i].position[2];

		m_vertices[i].texCoords.x = p_vertices[i].texCoords[0];
		m_vertices[i].texCoords.y= p_vertices[i].texCoords[1];

		m_vertices[i].normals.x = p_vertices[i].normals[0];
		m_vertices[i].normals.y = p_vertices[i].normals[1];
		m_vertices[i].normals.z = p_vertices[i].normals[2];
	}
	AddMaterial(p_materialIndex, 0);
	Upload(p_vertices, p_indices);
	ComputeBoundingSphereAndBox(p_vertices);
}
Rendering::Resources::Mesh::Mesh(const std::vector<Geometry::VertexPositionNormal>& p_vertices, const std::vector<uint32_t>& p_indices, uint32_t p_materialIndex, ::Rendering::Settings::EPrimitiveMode primitiveMode)
	: mPrimitiveMode(primitiveMode),
	m_vertexCount(p_vertices.size()),
	m_indicesCount(static_cast<uint32_t>(p_indices.size()))
{
	isIndex = m_indicesCount > 0;
	m_indices.resize(m_indicesCount);
	m_vertices.resize(m_vertexCount);
	uploadIndicesCount.resize(1);
	CreateResources();
	memcpy(m_indices.data(), p_indices.data(), p_indices.size() * sizeof(uint32_t));
	for (int i = 0; i < m_vertexCount; i++) {
		m_vertices[i].position = p_vertices[i].position;
		m_vertices[i].normals = p_vertices[i].normals;
	}
	AddMaterial(p_materialIndex, 0);
	Upload(p_vertices, p_indices);
	ComputeBoundingSphereAndBox(p_vertices);
}
Rendering::Resources::Mesh::Mesh(
	const std::vector<Geometry::VertexBVH>& p_vertices,
	const std::vector< uint32_t>& p_indices,
	uint32_t p_materialIndex,
	Settings::EPrimitiveMode primitiveMode
) : mPrimitiveMode(primitiveMode),
m_vertexCount(static_cast<uint32_t>(p_vertices.size())),
m_indicesCount(static_cast<uint32_t>(p_indices.size())),
m_vertices(std::move(p_vertices)),
m_indices(std::move(p_indices))
{
	isIndex = m_indicesCount > 0;

	CreateResources();
	uploadIndicesCount.resize(1);

	AddMaterial(p_materialIndex,0);
	Upload(p_vertices, p_indices);
}
Rendering::Resources::Mesh::~Mesh()
{
	delete m_bvh;
	m_bvh = nullptr;

	// The buffers and the arrays are front-end objects: dropping them frees their records and
	// queues the GL deletion to the side that owns the context (see Data::BufferObject /
	// Data::VertexArrayObject and Records::Destroy*). That side is not necessarily the thread
	// dropping the last reference - a loader, a feature rebuild on a job thread, closing a document.
}
	
void Rendering::Resources::Mesh::Bind(int index) const
{
	if (index < 0 || index >= static_cast<int>(m_vertexArrays.size()))
	{
		return;
	}
	m_vertexArrays[index].Bind();
}

void Rendering::Resources::Mesh::Unbind() const
{
	glBindVertexArray(0);
}

uint32_t Rendering::Resources::Mesh::GetVertexArrayId(int p_index) const
{
	if (p_index < 0 || p_index >= static_cast<int>(m_vertexArrays.size()))
	{
		return 0;
	}
	// Resolved from the handle, on the side that owns the context: the name is in the record.
	return m_vertexArrays[p_index].GetID();
}

uint32_t Rendering::Resources::Mesh::GetVertexCount() const
{
	return m_vertexCount;
}

uint32_t Rendering::Resources::Mesh::GetIndexCount(int index) const
{
	// Bounds-checked for the same reason as GetVertexArrayId: the count is only known once the
	// upload has run, and a draw replayed before that - or with a stale sub-mesh index - must get
	// zero rather than read past the end of the array.
	if (index < 0 || index >= static_cast<int>(uploadIndicesCount.size()))
	{
		return 0;
	}

	return uploadIndicesCount[index];
}

const Rendering::Geometry::BoundingSphere& Rendering::Resources::Mesh::GetBoundingSphere()
{

	return m_boundingSphere;
}

const Rendering::Geometry::bbox& Rendering::Resources::Mesh::GetBoundingBox()
{

	return m_boundingBox;
}

const std::vector<uint32_t>& Rendering::Resources::Mesh::GetMaterialIndex() const
{
	return m_materialIndex;
}

const std::vector<uint32_t>& Rendering::Resources::Mesh::GetSubRangeBufferIndex() const
{
	return m_subRangeIndex;
}

void Rendering::Resources::Mesh::AddMaterial(int materialIndex, int subRangeIndex)
{
	m_materialIndex.push_back(materialIndex);
	m_subRangeIndex.push_back(subRangeIndex);
}

void Rendering::Resources::Mesh::CreateResources()
{
	// Filament's VertexBuffer/IndexBuffer/VertexArray front objects: each is a front-end object that
	// owns its handle (see Data::BufferObject / Data::VertexArrayObject), and the create commands
	// give them their GL objects on the side that owns the context.
	m_vertexBuffer = Rendering::Data::BufferObject{ Rendering::Settings::EBufferType::VERTEX };

	m_vertexArrays.clear();
	m_vertexArrays.emplace_back();

	m_IndexBuffers.clear();
	m_IndexBuffers.emplace_back(Rendering::Settings::EBufferType::INDEX);
}

const Rendering::Backend::ResourceHandle<Rendering::Backend::HwVertexArray>& Rendering::Resources::Mesh::GetVertexArrayHandle(int p_index) const
{
	static const Rendering::Backend::ResourceHandle<Rendering::Backend::HwVertexArray> nullHandle{};
	if (p_index < 0 || p_index >= static_cast<int>(m_vertexArrays.size()))
	{
		return nullHandle;
	}
	return m_vertexArrays[p_index].GetHandle();
}

const Rendering::Backend::ResourceHandle<Rendering::Backend::HwBufferObject>& Rendering::Resources::Mesh::GetIndexBufferHandle(int p_index) const
{
	static const Rendering::Backend::ResourceHandle<Rendering::Backend::HwBufferObject> nullHandle{};
	if (p_index < 0 || p_index >= static_cast<int>(m_IndexBuffers.size()))
	{
		return nullHandle;
	}
	return m_IndexBuffers[p_index].GetHandle();
}

Maths::FVector3 Rendering::Resources::Mesh::GetVertexPosition(int index)
{
	return isIndex?m_vertices[m_indices[index]].position: m_vertices[index].position;
}

Rendering::Geometry::VertexBVH Rendering::Resources::Mesh::GetVertexBVH(int index)
{
	return isIndex ? m_vertices[m_indices[index]] : m_vertices[index];
}

std::vector<Rendering::Geometry::VertexBVH>& Rendering::Resources::Mesh::GetVerticesBVH()
{
	return m_vertices;
}

std::vector<uint32_t>& Rendering::Resources::Mesh::GetIndices()
{
	return m_indices;
	//if (isIndex)
	//{
	//	
	//}
	//else
	//{
	//	m_indices.resize(m_vertexCount);
	//	for (int i = 0; i < m_vertexCount; i++) {
	//		m_indices[i] = i;
	//	}
	//	isIndex = true;
	//	return m_indices;
	//}
	//return {};
}

Rendering::Settings::EPrimitiveMode Rendering::Resources::Mesh::GetPrimitiveMode() const
{
	return mPrimitiveMode;
}

void Rendering::Resources::Mesh::BuildBvh()
{
	// Built on demand: by the ray tracing when it builds its own data (see
	// BvhService::BuildTracingData), and by the queries that walk this mesh (see
	// EnsureMeshBvh there). Building it while the mesh was uploaded made every load pay
	// for it, and nothing else wants it.

	if (m_bvh) {
		delete m_bvh;
		m_bvh = nullptr;
	}
	
	
	if (mPrimitiveMode == ::Rendering::Settings::EPrimitiveMode::TRIANGLES) {
		int numTris = isIndex ? m_indicesCount / 3 : m_vertexCount / 3;
		if (numTris > 0) {
			m_bvh = new Geometry::SplitBvh(2.0f, 64, 0, 0.001f, 0);
			//为所有的三角形构建包围盒，然后在对所有的包围盒构建bvh
			std::vector<Geometry::bbox> bounds(numTris);

			for (int i = 0; i < numTris; ++i)
			{
				if (isIndex) {
					bounds[i].grow(m_vertices[m_indices[3 * i]].position);
					bounds[i].grow(m_vertices[m_indices[3 * i+1]].position);
					bounds[i].grow(m_vertices[m_indices[3 * i+2]].position);
				}
				else
				{			
					bounds[i].grow(m_vertices[3 * i].position);
					bounds[i].grow(m_vertices[3 * i + 1].position);
					bounds[i].grow(m_vertices[3 * i + 2].position);
				}
			}
			m_bvh->Build(&bounds[0], numTris);
		}
	}
	else if (mPrimitiveMode == ::Rendering::Settings::EPrimitiveMode::LINES) {
		int num = isIndex ? m_indicesCount / 2 : m_vertexCount / 2;
		if (num > 0) {
			m_bvh = new Geometry::SplitBvh(2.0f, 64, 0, 0.001f, 0);
			//为所有的线段构建包围盒，然后在对所有的包围盒构建bvh
			std::vector<Geometry::bbox> bounds(num);

			for (int i = 0; i < num; ++i)
			{
				if (isIndex) {
					bounds[i].grow(m_vertices[m_indices[2 * i]].position);
					bounds[i].grow(m_vertices[m_indices[2 * i + 1]].position);
				}
				else
				{
					bounds[i].grow(m_vertices[2 * i].position);
					bounds[i].grow(m_vertices[2 * i + 1].position);
				}
			}
			m_bvh->Build(&bounds[0], num);
		}
	}
}

Rendering::Geometry::Bvh* Rendering::Resources::Mesh::GetBvh()
{
	return m_bvh;
}

void Rendering::Resources::Mesh::UploadIndices(const std::vector<std::pair<int, int>>& childList,int index)
{
	std::vector<uint32_t> p_indices;;
	int numIndexs = 0;
	for (int i = 0;i < childList.size();i++) {
		numIndexs += childList[i].second;
	}
	p_indices.resize(numIndexs);
	int offet = 0;
	uint32_t* dst = p_indices.data();
	uint32_t* src = m_indices.data();

	for (int i = 0;i < childList.size();i++) {
		memcpy(dst+offet,src+childList[i].first, childList[i].second*4);
		offet += childList[i].second;
	}
	UploadIndices(p_indices,index);
}
void Rendering::Resources::Mesh::UploadIndices(const std::vector<uint32_t>& p_indices, int index)
{
	if (index < 0 || index >= static_cast<int>(m_IndexBuffers.size()))
	{
		return;
	}

	// CPU-side state first: the draw reads the count before the GPU half has run.
	const uint64_t indexBytes = p_indices.size() * sizeof(uint32_t);
	uploadIndicesCount[index] = static_cast<uint32_t>(p_indices.size());
	if (indexBytes == 0)
	{
		return;
	}

	// The index buffer is a front-end object: every call below is recorded as the command that
	// does it, and names the buffer's handle (see Data::BufferObject).
	m_IndexBuffers[index].Allocate(indexBytes);
	m_IndexBuffers[index].Upload(p_indices.data());
}
void Rendering::Resources::Mesh::AddSubRangeBuffer()
{
	m_vertexArrays.emplace_back();
	m_IndexBuffers.emplace_back(Rendering::Settings::EBufferType::INDEX);
	uploadIndicesCount.push_back(0);

	const int index = static_cast<int>(m_vertexArrays.size()) - 1;
	m_vertexArrays[index].SetLayout(
		m_vertexBuffer.GetHandle(), m_IndexBuffers[index].GetHandle(),
		std::to_array<Settings::VertexAttribute>({
			{ Settings::EDataType::FLOAT, 3 }, // position
			{ Settings::EDataType::FLOAT, 2 }, // texCoords
			{ Settings::EDataType::FLOAT, 3 }, // normal
			{ Settings::EDataType::FLOAT, 2 }, // domainId
		}));
}
void Rendering::Resources::Mesh::ComputeBoundingSphereAndBox()
{
	ComputeBoundingSphereAndBox(m_vertices);
}
void Rendering::Resources::Mesh::Upload(const std::vector<Geometry::Vertex>& p_vertices, const std::vector<uint32_t>& p_indices)
{
	const uint64_t vertexBytes = p_vertices.size() * sizeof(Geometry::Vertex);
	if (vertexBytes == 0)
	{
		return;
	}

	// CPU-side state first: callers read the stride before the GPU half ran.
	m_vertexStride = sizeof(Geometry::Vertex);

	// Every call below is recorded as the command that does it when the context lives on another
	// thread, and recorded as commands otherwise. The work names handles, so it stays
	// valid even if the front-end object that recorded it is gone before it runs.
	m_vertexBuffer.Allocate(vertexBytes);
	m_vertexBuffer.Upload(p_vertices.data());

	UploadIndices(p_indices);

	m_vertexArrays[0].SetLayout(
		m_vertexBuffer.GetHandle(), m_IndexBuffers[0].GetHandle(),
		std::to_array<Settings::VertexAttribute>({
			{ Settings::EDataType::FLOAT, 3 }, // position
			{ Settings::EDataType::FLOAT, 2 }, // texCoords
			{ Settings::EDataType::FLOAT, 3 }, // normal
			{ Settings::EDataType::FLOAT, 3 }, // tangent
			{ Settings::EDataType::FLOAT, 3 }  // bitangent
		}));
}
void Rendering::Resources::Mesh::Upload(const std::vector<Geometry::VertexPositionNormal>& p_vertices, const std::vector<uint32_t>& p_indices)
{
	const uint64_t vertexBytes = p_vertices.size() * sizeof(Geometry::VertexPositionNormal);
	if (vertexBytes == 0)
	{
		return;
	}

	m_vertexStride = sizeof(Geometry::VertexPositionNormal);

	m_vertexBuffer.Allocate(vertexBytes);
	m_vertexBuffer.Upload(p_vertices.data());

	UploadIndices(p_indices);

	m_vertexArrays[0].SetLayout(
		m_vertexBuffer.GetHandle(), m_IndexBuffers[0].GetHandle(),
		std::to_array<Settings::VertexAttribute>({
			{ Settings::EDataType::FLOAT, 3 }, // position
			{ Settings::EDataType::FLOAT, 3 }, // normal
		}));
}
void Rendering::Resources::Mesh::Upload(const std::vector<Geometry::VertexBVH>& p_vertices, const std::vector<uint32_t>& p_indices)
{
	const uint64_t vertexBytes = p_vertices.size() * sizeof(Geometry::VertexBVH);
	if (vertexBytes == 0)
	{
		return;
	}

	m_vertexStride = sizeof(Geometry::VertexBVH);

	m_vertexBuffer.Allocate(vertexBytes);
	m_vertexBuffer.Upload(p_vertices.data());

	UploadIndices(p_indices);

	m_vertexArrays[0].SetLayout(
		m_vertexBuffer.GetHandle(), m_IndexBuffers[0].GetHandle(),
		std::to_array<Settings::VertexAttribute>({
			{ Settings::EDataType::FLOAT, 3 }, // position
			{ Settings::EDataType::FLOAT, 2 }, // texCoords
			{ Settings::EDataType::FLOAT, 3 }, // normal
			{ Settings::EDataType::FLOAT, 2 }, // domainId
		}));
}
void Rendering::Resources::Mesh::ComputeBoundingSphereAndBox(const std::vector< Geometry::VertexPositionNormal>& p_vertices)
{
	m_boundingSphere.position = Maths::FVector3::Zero;
	m_boundingSphere.radius = 0.0f;


	if (!p_vertices.empty())
	{
		float minX = std::numeric_limits<float>::max();
		float minY = std::numeric_limits<float>::max();
		float minZ = std::numeric_limits<float>::max();

		float maxX = std::numeric_limits<float>::min();
		float maxY = std::numeric_limits<float>::min();
		float maxZ = std::numeric_limits<float>::min();

		for (const auto& vertex : p_vertices)
		{
			minX = std::min(minX, vertex.position[0]);
			minY = std::min(minY, vertex.position[1]);
			minZ = std::min(minZ, vertex.position[2]);

			maxX = std::max(maxX, vertex.position[0]);
			maxY = std::max(maxY, vertex.position[1]);
			maxZ = std::max(maxZ, vertex.position[2]);
		}

		m_boundingSphere.position = Maths::FVector3{ minX + maxX, minY + maxY, minZ + maxZ } / 2.0f;
		m_boundingBox = Geometry::bbox(Maths::FVector3{ minX , minY , minZ }, Maths::FVector3{ maxX,  maxY, maxZ });
		for (const auto& vertex : p_vertices)
		{
			const auto& position = reinterpret_cast<const Maths::FVector3&>(vertex.position);
			m_boundingSphere.radius = std::max(m_boundingSphere.radius, Maths::FVector3::Distance(m_boundingSphere.position, position));
		}
	}
}
void Rendering::Resources::Mesh::ComputeBoundingSphereAndBox(const std::vector< Geometry::Vertex>& p_vertices)
{
	m_boundingSphere.position = Maths::FVector3::Zero;
	m_boundingSphere.radius = 0.0f;
	

	if (!p_vertices.empty())
	{
		float minX = std::numeric_limits<float>::max();
		float minY = std::numeric_limits<float>::max();
		float minZ = std::numeric_limits<float>::max();

		float maxX = std::numeric_limits<float>::min();
		float maxY = std::numeric_limits<float>::min();
		float maxZ = std::numeric_limits<float>::min();

		for (const auto& vertex : p_vertices)
		{
			minX = std::min(minX, vertex.position[0]);
			minY = std::min(minY, vertex.position[1]);
			minZ = std::min(minZ, vertex.position[2]);

			maxX = std::max(maxX, vertex.position[0]);
			maxY = std::max(maxY, vertex.position[1]);
			maxZ = std::max(maxZ, vertex.position[2]);
		}

		m_boundingSphere.position = Maths::FVector3{ minX + maxX, minY + maxY, minZ + maxZ } / 2.0f;
		m_boundingBox = Geometry::bbox(Maths::FVector3{ minX , minY , minZ  }, Maths::FVector3{  maxX,  maxY, maxZ });
		for (const auto& vertex : p_vertices)
		{
			const auto& position = reinterpret_cast<const Maths::FVector3&>(vertex.position);
			m_boundingSphere.radius = std::max(m_boundingSphere.radius, Maths::FVector3::Distance(m_boundingSphere.position, position));
		}
	}
}
void Rendering::Resources::Mesh::ComputeBoundingSphereAndBox(const std::vector< Geometry::VertexBVH>& p_vertices)
{
	m_boundingSphere.position = Maths::FVector3::Zero;
	m_boundingSphere.radius = 0.0f;
	if (!p_vertices.empty())
	{
		float minX = std::numeric_limits<float>::max();
		float minY = std::numeric_limits<float>::max();
		float minZ = std::numeric_limits<float>::max();

		float maxX = std::numeric_limits<float>::min();
		float maxY = std::numeric_limits<float>::min();
		float maxZ = std::numeric_limits<float>::min();

		for (const auto& vertex : p_vertices)
		{
			minX = std::min(minX, vertex.position[0]);
			minY = std::min(minY, vertex.position[1]);
			minZ = std::min(minZ, vertex.position[2]);

			maxX = std::max(maxX, vertex.position[0]);
			maxY = std::max(maxY, vertex.position[1]);
			maxZ = std::max(maxZ, vertex.position[2]);
		}

		m_boundingSphere.position = Maths::FVector3{ minX + maxX, minY + maxY, minZ + maxZ } / 2.0f;
		m_boundingBox = Geometry::bbox(Maths::FVector3{ minX , minY , minZ }, Maths::FVector3{ maxX,  maxY, maxZ });
		for (const auto& vertex : p_vertices)
		{
			m_boundingSphere.radius = std::max(m_boundingSphere.radius, Maths::FVector3::Distance(m_boundingSphere.position, vertex.position));
		}
	}
}


