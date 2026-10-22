#include "Rendering/Resources/Loaders/ModelLoader.h"

Rendering::Resources::Parsers::AssimpParser Rendering::Resources::Loaders::ModelLoader::__ASSIMP;

#include "Rendering/Geometry/TangentSpace.h"
Rendering::Resources::Model* Rendering::Resources::Loaders::ModelLoader::Create(const std::string& p_filepath, Parsers::EModelParserFlags p_parserFlags)
{
	Model* result = new Model(p_filepath);

	if (__ASSIMP.LoadModel(p_filepath, result->m_meshes, result->m_materialNames, p_parserFlags))
	{
		result->ComputeBoundingSphere();
		return result;
	}

	delete result;

	return nullptr;
}

void Rendering::Resources::Loaders::ModelLoader::Reload(Model& p_model, const std::string& p_filePath, Parsers::EModelParserFlags p_parserFlags)
{
	Model* newModel = Create(p_filePath, p_parserFlags);

	if (newModel)
	{
		p_model.m_meshes = newModel->m_meshes;
		p_model.m_materialNames = newModel->m_materialNames;
		p_model.m_boundingSphere = newModel->m_boundingSphere;
		newModel->m_meshes.clear();
		delete newModel;
	}
}

bool Rendering::Resources::Loaders::ModelLoader::Destroy(Model*& p_modelInstance)
{
	if (p_modelInstance)
	{
		delete p_modelInstance;
		p_modelInstance = nullptr;

		return true;
	}

	return false;
}

Rendering::Resources::Model* Rendering::Resources::Loaders::ModelLoader::LoadFromMemory(const std::vector<float>& v, const std::vector<unsigned int>& i)
{
	Model* result = new Model("Memory");
	int numFaces = i.size() / 3;
	std::vector<Geometry::Vertex> vertices(numFaces*3);
	for (int k = 0; k < numFaces; k++) {
		for (int m = 0; m < 3; m++) {
            int index = 3 * k+m;
			vertices[index].position[0] = v[i[index]];
			vertices[index].position[1] = v[i[index]+1];
			vertices[index].position[2] = v[i[index]+2];
		}
		Maths::FVector3 edge1 = Maths::FVector3::Substract(Maths::FVector3(vertices[3 * k + 1].position[0], vertices[3 * k + 1].position[1], vertices[3 * k + 1].position[2]),
			Maths::FVector3(vertices[3 * k + 0].position[0], vertices[3 * k + 0].position[1], vertices[3 * k + 0].position[2]));
		Maths::FVector3 edge2 = Maths::FVector3::Substract(Maths::FVector3(vertices[3 * k + 2].position[0], vertices[3 * k + 2].position[1], vertices[3 * k + 2].position[2]),
			Maths::FVector3(vertices[3 * k + 0].position[0], vertices[3 * k + 0].position[1], vertices[3 * k + 0].position[2]));
		Maths::FVector3 normal = Maths::FVector3::Cross(edge1, edge2);
		normal = Maths::FVector3::Normalize(normal);
		for (int m = 0; m < 3; m++) {
			int index = 3 * k + m;
			vertices[index].normals[0] = normal.x;
			vertices[index].normals[1] = normal.y;
			vertices[index].normals[2] = normal.z;
			vertices[index].texCoords[0] = 0.0f;
			vertices[index].texCoords[1] = 0.0f;
		}	
	}
	
	result->m_meshes.push_back(new Mesh(vertices, i, 0));
	result->m_materialNames.push_back("Default");
	result->ComputeBoundingSphere();
	return result;

}
Rendering::Resources::Model* Rendering::Resources::Loaders::ModelLoader::LoadFromMemory(const std::vector<Maths::FVector3>& vertex, const std::vector<Maths::FVector2>& uv, const std::vector<unsigned int>& i)
{
	Model* result = new Model("Memory");
	int numFaces = i.size() / 3;

	std::vector<Geometry::Vertex> vertices(numFaces * 3);
	for (int k = 0; k < numFaces; k++) {
		for (int m = 0; m < 3; m++) {
			int index = 3 * k + m;
			vertices[index].position[0] = vertex[i[index]].x;
			vertices[index].position[1] = vertex[i[index]].y;
			vertices[index].position[2] = vertex[i[index]].z;
			vertices[index].texCoords[0] = uv[i[index]].x;
			vertices[index].texCoords[1] = uv[i[index]].y;
		}
		Maths::FVector3 edge1 = Maths::FVector3::Substract(Maths::FVector3(vertices[3 * k + 1].position[0], vertices[3 * k + 1].position[1], vertices[3 * k + 1].position[2]),
			Maths::FVector3(vertices[3 * k + 0].position[0], vertices[3 * k + 0].position[1], vertices[3 * k + 0].position[2]));
		Maths::FVector3 edge2 = Maths::FVector3::Substract(Maths::FVector3(vertices[3 * k + 2].position[0], vertices[3 * k + 2].position[1], vertices[3 * k + 2].position[2]),
			Maths::FVector3(vertices[3 * k + 0].position[0], vertices[3 * k + 0].position[1], vertices[3 * k + 0].position[2]));

		Maths::FVector3 normal = Maths::FVector3::Cross(edge1, edge2);

		normal = Maths::FVector3::Normalize(normal);
		for (int m = 0; m < 3; m++) {
			int index = 3 * k + m;
			vertices[index].normals[0] = normal.x;
			vertices[index].normals[1] = normal.y;
			vertices[index].normals[2] = normal.z;
		}
	}
	std::vector<uint32_t>indices(i.size());;
	for (int k = 0;k < i.size();k++) {
		indices[k] = k;
	}
	result->m_meshes.push_back(new Mesh(vertices, indices, 0));
	result->m_materialNames.push_back("Default");
	result->ComputeBoundingSphere();
	return result;
}

Rendering::Resources::Model* Rendering::Resources::Loaders::ModelLoader::LoadFromMemory(const std::vector<Maths::FVector3>& vertex, const std::vector<unsigned int>& i)
{
	Model* result = new Model("Memory");
	int numFaces = i.size() / 3;

	std::vector<Geometry::Vertex> vertices(numFaces*3);
	for (int k = 0; k < numFaces; k++) {
		for (int m = 0; m < 3; m++) {
			int index = 3 * k+m;
			vertices[index].position[0] = vertex[i[index]].x;
			vertices[index].position[1] = vertex[i[index]].y;
			vertices[index].position[2] = vertex[i[index]].z;
		}
		Maths::FVector3 edge1 = Maths::FVector3::Substract(Maths::FVector3(vertices[3 * k + 1].position[0], vertices[3 * k + 1].position[1], vertices[3 * k + 1].position[2]),
			Maths::FVector3(vertices[3 * k + 0].position[0], vertices[3 * k + 0].position[1], vertices[3 * k + 0].position[2]));
		Maths::FVector3 edge2 = Maths::FVector3::Substract(Maths::FVector3(vertices[3 * k + 2].position[0], vertices[3 * k + 2].position[1], vertices[3 * k + 2].position[2]),
			Maths::FVector3(vertices[3 * k + 0].position[0], vertices[3 * k + 0].position[1], vertices[3 * k + 0].position[2]));

		Maths::FVector3 normal = Maths::FVector3::Cross(edge1, edge2);

		normal = Maths::FVector3::Normalize(normal);
		for (int m = 0; m < 3; m++) {
			int index = 3 * k + m;
			vertices[index].normals[0] = normal.x;
			vertices[index].normals[1] = normal.y;
			vertices[index].normals[2] = normal.z;
			vertices[index].texCoords[0] = 0.0f;
			vertices[index].texCoords[1] = 0.0f;
		}
	}
	std::vector<uint32_t>indices(i.size());;
	for (int k = 0;k < i.size();k++) {
		indices[k] = k;
	}
	result->m_meshes.push_back(new Mesh(vertices, indices, 0));
	result->m_materialNames.push_back("Default");
	result->ComputeBoundingSphere();
	return result;
}
Rendering::Resources::Model* Rendering::Resources::Loaders::ModelLoader::LoadFromMemory(const std::vector<Maths::FVector3>& vertex, const std::vector<Maths::FVector3>& normal, const std::vector<unsigned int>& i)
{
	Model* result = new Model("Memory");
	std::vector<Geometry::Vertex> vertices(vertex.size());
	std::vector<uint32_t> indices = i;
	for (int k = 0; k < vertices.size(); k++) {
		vertices[k].position[0] = vertex[k].x;
		vertices[k].position[1] = vertex[k].y;
		vertices[k].position[2] = vertex[k].z;
		vertices[k].normals[0] = normal[k].x;
		vertices[k].normals[1] = normal[k].y;
		vertices[k].normals[2] = normal[k].z;
		vertices[k].texCoords[0] = 0;
		vertices[k].texCoords[1] = 0;
	}
	GenerateTangents(vertices.data(), sizeof(Geometry::Vertex), indices.data(), 4, 0, indices.size(), 20, 12, 32);
	result->m_meshes.push_back(new Mesh(vertices, indices, 0));
	result->m_materialNames.push_back("Default");
	result->ComputeBoundingSphere();
	return result;
}
Rendering::Resources::Model* Rendering::Resources::Loaders::ModelLoader::LoadFromMemory(const std::vector<Maths::FVector3>& vertex, const std::vector<Maths::FVector3>& normal, const std::vector<Maths::FVector2>& uv, const std::vector<unsigned int>& i)
{
	Model* result = new Model("Memory");
	std::vector<Geometry::Vertex> vertices(vertex.size());
	std::vector<uint32_t> indices = i;
	for (int k = 0; k < vertices.size(); k++) {
		vertices[k].position[0] = vertex[k].x;
		vertices[k].position[1] = vertex[k].y;
		vertices[k].position[2] = vertex[k].z;
		vertices[k].normals[0] = normal[k].x;
		vertices[k].normals[1] = normal[k].y;
		vertices[k].normals[2] = normal[k].z;
		vertices[k].texCoords[0] = uv[k].x;
		vertices[k].texCoords[1] = uv[k].y;
	}
	GenerateTangents(vertices.data(), sizeof(Geometry::Vertex), indices.data(), 4, 0, indices.size(), 20, 12, 32);
	result->m_meshes.push_back(new Mesh(vertices, indices, 0));
	result->m_materialNames.push_back("Default");
	result->ComputeBoundingSphere();
	return result;
}
