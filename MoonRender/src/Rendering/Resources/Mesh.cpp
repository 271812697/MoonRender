#include <array>
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
	m_vertexArrays.resize(1);
	m_IndexBuffers.resize(1);
	m_vertexArrays[0] = std::make_unique<HAL::VertexArray>();
	m_IndexBuffers[0] = std::make_unique <HAL::IndexBuffer>();

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
	m_vertexArrays.resize(1);
	m_IndexBuffers.resize(1);
	m_vertexArrays[0] = std::make_unique<HAL::VertexArray>();
	m_IndexBuffers[0] = std::make_unique <HAL::IndexBuffer>();
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

	m_vertexArrays.resize(1);
	m_IndexBuffers.resize(1);
	m_vertexArrays[0] = std::make_unique<HAL::VertexArray>();
	m_IndexBuffers[0] = std::make_unique <HAL::IndexBuffer>();
	uploadIndicesCount.resize(1);

	AddMaterial(p_materialIndex,0);
	Upload(p_vertices, p_indices);
}
Rendering::Resources::Mesh::~Mesh()
{
	delete m_bvh;
}
	
void Rendering::Resources::Mesh::Bind(int index) const
{
	m_vertexArrays[index]->Bind();
}

void Rendering::Resources::Mesh::Unbind() const
{
	m_vertexArrays[0]->Unbind();
}

uint32_t Rendering::Resources::Mesh::GetVertexCount() const
{
	return m_vertexCount;
}

uint32_t Rendering::Resources::Mesh::GetIndexCount(int index) const
{

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

Rendering::HAL::VertexArray& Rendering::Resources::Mesh::getVertexArray(int index)
{
	return *m_vertexArrays[index];
}

Rendering::HAL::VertexBuffer& Rendering::Resources::Mesh::getVertexBuffer()
{
	return m_vertexBuffer;
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
	if (m_IndexBuffers[index]->Allocate(p_indices.size() * sizeof(uint32_t)))
	{
		m_IndexBuffers[index]->Upload(p_indices.data());
	}
	else
	{
		("Empty index buffer!");
	}
	uploadIndicesCount[index] = p_indices.size();
}
void Rendering::Resources::Mesh::AddSubRangeBuffer()
{
	m_vertexArrays.emplace_back();;
	m_IndexBuffers.emplace_back();
	m_vertexArrays.back() = std::make_unique<HAL::VertexArray>();
	m_IndexBuffers.back() = std::make_unique <HAL::IndexBuffer>();
	uploadIndicesCount.push_back(0);
	int index = m_vertexArrays.size() - 1;
	m_vertexArrays[index]->SetLayout(std::to_array<Settings::VertexAttribute>({
			{ Settings::EDataType::FLOAT, 3 }, // position
			{ Settings::EDataType::FLOAT, 2 }, // texCoords
			{ Settings::EDataType::FLOAT, 3 }, // normal
			{ Settings::EDataType::FLOAT, 2 }, // domainId
		}), m_vertexBuffer, *m_IndexBuffers[index]);
}
void Rendering::Resources::Mesh::ComputeBoundingSphereAndBox()
{
	ComputeBoundingSphereAndBox(m_vertices);
}
void Rendering::Resources::Mesh::Upload(const std::vector<Geometry::Vertex>& p_vertices, const std::vector<uint32_t>& p_indices)
{
	if (m_vertexBuffer.Allocate(p_vertices.size()*sizeof(Geometry::Vertex)))
	{
		m_vertexStride = sizeof(Geometry::Vertex);
		m_vertexBuffer.Upload(p_vertices.data());

		UploadIndices(p_indices);
		m_vertexArrays[0]->SetLayout(std::to_array<Settings::VertexAttribute>({
			{ Settings::EDataType::FLOAT, 3 }, // position
			{ Settings::EDataType::FLOAT, 2 }, // texCoords
			{ Settings::EDataType::FLOAT, 3 }, // normal
			{ Settings::EDataType::FLOAT, 3 }, // tangent
			{ Settings::EDataType::FLOAT, 3 }  // bitangent
			}), m_vertexBuffer, *m_IndexBuffers[0]);
	}
	else
	{
		//("Empty vertex buffer!");
	}
}
void Rendering::Resources::Mesh::Upload(const std::vector<Geometry::VertexPositionNormal>& p_vertices, const std::vector<uint32_t>& p_indices)
{
	if (m_vertexBuffer.Allocate(p_vertices.size() * sizeof(Geometry::VertexPositionNormal)))
	{
		m_vertexStride = sizeof(Geometry::VertexPositionNormal);
		m_vertexBuffer.Upload(p_vertices.data());

		UploadIndices(p_indices);
		m_vertexArrays[0]->SetLayout(std::to_array<Settings::VertexAttribute>({
			{ Settings::EDataType::FLOAT, 3 }, // position
			{ Settings::EDataType::FLOAT, 3 }, // normal
			}), m_vertexBuffer, *m_IndexBuffers[0]);
	}
	else
	{
		//("Empty vertex buffer!");
	}
}
void Rendering::Resources::Mesh::Upload(const std::vector<Geometry::VertexBVH>& p_vertices, const std::vector<uint32_t>& p_indices)
{
	if (m_vertexBuffer.Allocate(p_vertices.size() * sizeof(Geometry::VertexBVH)))
	{
		m_vertexStride = sizeof(Geometry::VertexBVH);
		m_vertexBuffer.Upload(p_vertices.data());

		UploadIndices(p_indices);
		m_vertexArrays[0]->SetLayout(std::to_array<Settings::VertexAttribute>({
			{ Settings::EDataType::FLOAT, 3 }, // position
			{ Settings::EDataType::FLOAT, 2 }, // texCoords
			{ Settings::EDataType::FLOAT, 3 }, // normal
			{ Settings::EDataType::FLOAT, 2 }, // domainId
			}), m_vertexBuffer, *m_IndexBuffers[0]);
	}
	else
	{
		//("Empty vertex buffer!");
	}
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
		BuildBvh();
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
		BuildBvh();
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
		BuildBvh();
	}
}
