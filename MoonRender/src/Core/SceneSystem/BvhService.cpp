#include <algorithm>
#include <cmath>
#include <glad/glad.h>
#include <string>
#include <tinyxml2.h>
#include <tracy/Tracy.hpp>
#include <Rendering/HAL/TextureHandle.h>
#include <Rendering/Resources/Texture.h>
#include <Core/ECS/Components/CAmbientSphereLight.h>
#include <Core/ECS/Components/CDirectionalLight.h>
#include <Core/ECS/Components/CMaterialRenderer.h>
#include <Core/ECS/Components/CBatchMeshTriangle.h>
#include <Core/ECS/Components/CBatchMeshLine.h>
#include <Core/Global/ServiceLocator.h>
#include <Core/ResourceManagement/MaterialManager.h>
#include <Core/ResourceManagement/ModelManager.h>
#include <Core/SceneSystem/Scene.h>
#include <Core/SceneSystem/BvhService.h>
#include "stb_image/stb_image_resize.h"
#include <fstream>
namespace Core::SceneSystem
{

	bool exportObjOnlyVertices(
		const std::string& filename,
		const float* vertices,
		const unsigned int* indices,
		size_t vertexCount,
		size_t indexCount
	) {
		// 前置校验
		if (vertices == nullptr || indices == nullptr) {
			std::cerr << "错误：顶点/索引数组为空！" << std::endl;
			return false;
		}
		if (vertexCount == 0 || indexCount == 0) {
			std::cerr << "错误：顶点/索引数量不能为0！" << std::endl;
			return false;
		}
		if (indexCount % 3 != 0) {
			std::cerr << "错误：索引数量必须是3的倍数（三角面）！当前数量：" << indexCount << std::endl;
			return false;
		}

		try {
			// 打开文件（覆盖写入）
			std::ofstream objFile(filename);
			if (!objFile.is_open()) {
				throw std::runtime_error("无法打开文件：" + filename);
			}

			// 写入文件头（可选，方便查看信息）
			objFile << "# 自动生成的OBJ模型（仅顶点坐标）\n";
			objFile << "# 顶点数：" << vertexCount << " | 三角面数：" << (indexCount / 3) << "\n\n";

			// 第一步：写入顶点坐标（OBJ格式：v x y z）
			for (size_t i = 0; i < vertexCount; ++i) {
				size_t offset = i * 3; // 每个顶点占3个float（x,y,z）
				objFile << "v "
					<< vertices[offset] << " "    // x
					<< vertices[offset + 1] << " "// y
					<< vertices[offset + 2] << "\n";// z
			}
			objFile << "\n"; // 空行分隔，提升可读性

			// 第二步：写入三角面索引（OBJ索引从1开始，需+1转换）
			for (size_t i = 0; i < indexCount; i += 3) {
				objFile << "f "
					<< (indices[i] + 1) << " "    // 第一个顶点索引
					<< (indices[i + 1] + 1) << " "// 第二个顶点索引
					<< (indices[i + 2] + 1) << "\n";// 第三个顶点索引
			}

			// 关闭文件
			objFile.close();
			std::cout << "✅ OBJ文件导出成功！路径：" << filename << std::endl;
			return true;
		}
		catch (const std::exception& e) {
			std::cerr << "❌ 导出失败：" << e.what() << std::endl;
			return false;
		}
	}
	// Same pastel palette as CTopoShape::DomainColorForSolid, keyed by the
	// per-vertex domain index (domainId.x) so each solid gets a distinct color.
	Maths::FVector3 DomainHsvToRgb(float p_hue, float p_saturation, float p_value)
	{
	const int i = static_cast<int>(p_hue * 6.0f);
	const float f = p_hue * 6.0f - i;
	const float p = p_value * (1.0f - p_saturation);
	const float q = p_value * (1.0f - f * p_saturation);
	const float t = p_value * (1.0f - (1.0f - f) * p_saturation);
	switch (i % 6)
	{
	case 0: return { p_value, t, p };
	case 1: return { q, p_value, p };
	case 2: return { p, p_value, t };
	case 3: return { p, q, p_value };
	case 4: return { t, p, p_value };
	default: return { p_value, p, q };
	}
	}

	Maths::FVector4 DomainColorForTriangle(int p_domainIndex)
	{
	if (p_domainIndex < 0)
	{
	return Maths::FVector4{ 0.85f, 0.85f, 0.88f, 1.0f };
	}
	if (p_domainIndex == 0)
	{
	return Maths::FVector4{ 1.0f, 1.0f, 1.0f, 1.0f };
	}
	const float hue = std::fmod(p_domainIndex * 137.508f, 360.0f) / 360.0f;
	const auto rgb = DomainHsvToRgb(hue, 0.38f, 0.95f);
	return Maths::FVector4{ rgb.x, rgb.y, rgb.z, 1.0f };
	}
	BvhService::BvhService(Scene* sc):scene(sc)
	{
	}
	void BvhService::AddMaterial(::Core::Resources::Material* material)
	{
		Material tempMat;
		if (material) {
			auto& propsMap=material->GetProperties();
			auto it = propsMap.find("u_Albedo");
			if (it != propsMap.end()) {
				tempMat.baseColor=std::get<Maths::FVector4>(it->second.value).ToFVector3();
				tempMat.opacity = std::get<Maths::FVector4>(it->second.value).w;
			
			}
			it = propsMap.find("u_EmissiveColor");
			if (it != propsMap.end()) {
				tempMat.emission = std::get<Maths::FVector3>(it->second.value);
			}
			it = propsMap.find("u_Metallic");
			if (it != propsMap.end()) {
				tempMat.metallic= std::get<float>(it->second.value);
			}
			it = propsMap.find("u_Roughness");
			if (it != propsMap.end()) {
				tempMat.roughness = std::get<float>(it->second.value);
			}
			//u_RefractionIndex
			it = propsMap.find("u_RefractionIndex");
			if (it != propsMap.end()) {
				tempMat.ior = std::get<float>(it->second.value);
			}

			//Albedo Texture
			it = propsMap.find("u_AlbedoMap");
			if (it!=propsMap.end()) {
				::Rendering::HAL::Texture* handle = nullptr;
				if (auto textureHandle = std::get_if<::Rendering::HAL::TextureHandle*>(&it->second.value))
				{
					handle = static_cast<::Rendering::HAL::Texture*>(*textureHandle);
				}
				else if (auto texture = std::get_if<::Rendering::Resources::Texture*>(&it->second.value))
				{
					if (*texture != nullptr)
					{
						handle = &(*texture)->GetTexture();					
					}
				}
				if (handle) {
					tempMat.baseColorTexId=AddTexture(handle);
				}
			}
			it = propsMap.find("u_RoughnessMap");
			if (it != propsMap.end()) {
				::Rendering::HAL::Texture* handle = nullptr;
				if (auto textureHandle = std::get_if<::Rendering::HAL::TextureHandle*>(&it->second.value))
				{
					handle = static_cast<::Rendering::HAL::Texture*>(*textureHandle);
				}
				else if (auto texture = std::get_if<::Rendering::Resources::Texture*>(&it->second.value))
				{
					if (*texture != nullptr)
					{
						handle = &(*texture)->GetTexture();
					}
				}
				if (handle) {
					tempMat.metallicRoughnessTexID = AddTexture(handle);
				}
			}
			it = propsMap.find("u_EmissiveMap");
			if (it != propsMap.end()) {
				::Rendering::HAL::Texture* handle = nullptr;
				if (auto textureHandle = std::get_if<::Rendering::HAL::TextureHandle*>(&it->second.value))
				{
					handle = static_cast<::Rendering::HAL::Texture*>(*textureHandle);
				}
				else if (auto texture = std::get_if<::Rendering::Resources::Texture*>(&it->second.value))
				{
					if (*texture != nullptr)
					{
						handle = &(*texture)->GetTexture();
					}
				}
				if (handle) {
					tempMat.emissionmapTexID = AddTexture(handle);
				}
			}
			it = propsMap.find("u_NormalMap");
			if (it != propsMap.end()) {
				::Rendering::HAL::Texture* handle = nullptr;
				if (auto textureHandle = std::get_if<::Rendering::HAL::TextureHandle*>(&it->second.value))
				{
					handle = static_cast<::Rendering::HAL::Texture*>(*textureHandle);
				}
				else if (auto texture = std::get_if<::Rendering::Resources::Texture*>(&it->second.value))
				{
					if (*texture != nullptr)
					{
						handle = &(*texture)->GetTexture();
					}
				}
				if (handle) {
					tempMat.normalmapTexID= AddTexture(handle);
				}
			}		
		}
		tempMat.roughness = 0.05;
		tempMat.metallic = 0.8;
		//tempMat.baseColor = { 136 / 255.0f,139 / 255.0f,141 / 255.0f };
		tempMat.baseColor = { 1.0,1.0f,1.0f };
		materials.push_back(tempMat);
	}
	int	 BvhService::AddTexture(::Rendering::HAL::Texture* tex)
	{
		int id = -1;
		// Check if texture was already loaded
		for (int i = 0; i < textures.size(); i++)
			if (textures[i] == tex)
				return i;
		id = textures.size();
		textures.push_back(tex);
		return id;
	}
	void BvhService::ProcessBLAS() {
			int nodeCnt = 0;
			for (int i = 0; i < triMeshes.size(); i++)
				nodeCnt += triMeshes[i]->GetBvh()->m_nodecnt;
			topLevelIndex = nodeCnt;
			// reserve space for top level nodes
			nodeCnt += 2 * triMeshInstances.size();
			nodes.resize(nodeCnt);

			int bvhRootIndex = 0;
			curTriIndex = 0;

			for (int i = 0; i < triMeshes.size(); i++)
			{
				::Rendering::Resources::Mesh* mesh = triMeshes[i];
				curNode = bvhRootIndex;

				bvhRootStartIndices.push_back(bvhRootIndex);
				bvhRootIndex += mesh->GetBvh()->m_nodecnt;

				ProcessBLASNodes(mesh->GetBvh()->m_root);
				curTriIndex += mesh->GetBvh()->GetNumIndices();
			}

		}
	void BvhService::ProcessTLAS() {
		// Nothing to walk when the top level has no root, which is what an empty
		// triangle set leaves behind (see Process). Walking it anyway is a null
		// dereference rather than an empty scene.
		if (topLevelBvh == nullptr || topLevelBvh->m_root == nullptr) {
			return;
		}
		curNode = topLevelIndex;
		ProcessTLASNodes(topLevelBvh->m_root);
	}
	int BvhService::ProcessBLASNodes(const ::Rendering::Geometry::Bvh::Node* node) {
		::Rendering::Geometry::bbox bbox = node->bounds;

		nodes[curNode].bboxmin = bbox.pmin;
		nodes[curNode].bboxmax = bbox.pmax;
		nodes[curNode].LRLeaf.z = 0;

		int index = curNode;
		//BLAS的叶子节点z=1 x y为二级索引
		if (node->type == ::Rendering::Geometry::Bvh::NodeType::kLeaf)
		{
			nodes[curNode].LRLeaf.x = curTriIndex + node->startidx;
			nodes[curNode].LRLeaf.y = node->numprims;
			nodes[curNode].LRLeaf.z = 1;
		}
		else
		{
			curNode++;
			nodes[index].LRLeaf.x = ProcessBLASNodes(node->lc);
			curNode++;
			nodes[index].LRLeaf.y = ProcessBLASNodes(node->rc);
		}
		return index;
	}
	int BvhService::ProcessTLASNodes(const ::Rendering::Geometry::Bvh::Node* node) {
		::Rendering::Geometry::bbox bbox = node->bounds;

		nodes[curNode].bboxmin = bbox.pmin;
		nodes[curNode].bboxmax = bbox.pmax;
		nodes[curNode].LRLeaf.z = 0;

		int index = curNode;

		if (node->type == ::Rendering::Geometry::Bvh::NodeType::kLeaf)
		{
			//叶子节点
			int instanceIndex = topLevelBvh->m_packed_indices[node->startidx];
			int meshIndex = triMeshInstances[instanceIndex].meshID;
			int materialID = triMeshInstances[instanceIndex].materialID;

			nodes[curNode].LRLeaf.x = bvhRootStartIndices[meshIndex];
			nodes[curNode].LRLeaf.y = materialID;
			nodes[curNode].LRLeaf.z = -instanceIndex - 1;
		}
		else
		{
			//内部节点存储左右子节点的索引
			curNode++;
			nodes[index].LRLeaf.x = ProcessTLASNodes(node->lc);
			curNode++;
			nodes[index].LRLeaf.y = ProcessTLASNodes(node->rc);
		}
		return index;
	}
	void BvhService::UpdateTLAS(const ::Rendering::Geometry::Bvh* topLevelBvh, const std::vector<MeshInstance>& instances) {
		this->topLevelBvh = topLevelBvh;
		triMeshInstances = instances;
		curNode = topLevelIndex;
		ProcessTLASNodes(topLevelBvh->m_root);
	}
	void BvhService::Process(const std::vector<::Rendering::Geometry::bbox>& boxs, const std::vector<::Rendering::Resources::Mesh*>& sceneMeshes, const std::vector<MeshInstance>& instances) {
		
		m_sceneBvh->Build(boxs.data(),boxs.size());;
		mSceneMeshes = sceneMeshes;
		mSceneMeshInstances = instances;

		triMeshes.clear();
		triMeshInstances.clear();
		buildProblems.clear();
		std::vector<::Rendering::Geometry::bbox> triBoxs;
		std::unordered_map<::Rendering::Resources::Mesh*, int>triMeshIndex;
		for (int i = 0;i < sceneMeshes.size();i++) {
			// A mesh without a BVH has no triangles to index - an empty topology batch
			// (a sketch whose profile did not come out as a face, say) is the usual
			// one. It stays out of the traced set, and its instances are skipped below
			// with it so the mesh indices stay in step.
			if (sceneMeshes[i]->GetPrimitiveMode() == ::Rendering::Settings::EPrimitiveMode::TRIANGLES
				&& sceneMeshes[i]->GetBvh() != nullptr) {
				triMeshIndex[sceneMeshes[i]] = triMeshes.size();
				triMeshes.push_back(sceneMeshes[i]);
			}
		}
		for (int i = 0;i < instances.size();i++) {
			int meshId = instances[i].meshID;
			// An instance names its mesh by index, and a mismatch is not something to
			// read past the end of the array for: it is reported and left out.
			if (meshId < 0 || meshId >= static_cast<int>(sceneMeshes.size())) {
				buildProblems.push_back(
					"instance " + std::to_string(i) + " names mesh " + std::to_string(meshId)
					+ ", but the scene holds " + std::to_string(sceneMeshes.size())
					+ " mesh(es); it is left out"
				);
				continue;
			}
			auto& mesh = sceneMeshes[meshId];
			if (mesh->GetPrimitiveMode() != ::Rendering::Settings::EPrimitiveMode::TRIANGLES) {
				continue;
			}
			const auto triIndex = triMeshIndex.find(mesh);
			if (triIndex == triMeshIndex.end()) {
				continue;  // no BVH: nothing of it to trace
			}
			auto triMeshInstance = instances[i];
			triMeshInstance.meshID = triIndex->second;
			triMeshInstances.push_back(triMeshInstance);
			triBoxs.push_back(boxs[i]);
		}

		m_sceneTriBvh->Build(triBoxs.data(),triBoxs.size());
		this->topLevelBvh = m_sceneTriBvh;
		// A scene whose meshes all fell out of the traced set - no triangle batch among
		// them, or a hidden body whose only visible feature is a sketch wire - leaves an
		// empty triangle BVH. There is no top level to walk then, and walking the null
		// root it holds used to end the process (see ProcessTLAS).
		if (triMeshInstances.empty() || m_sceneTriBvh->m_root == nullptr) {
			topLevelIndex = 0;
			bvhRootStartIndices.clear();
			nodes.clear();
		}
		else {
			ProcessBLAS();
			ProcessTLAS();
		}
			
		//Copy Mesh vertex data to a batch buffer
		vertIndices.clear();
		triangleDomainColors.clear();
		verticesUVX.clear();
		normalsUVY.clear();
		transforms.clear();
		int verticesCnt = 0;
		for (int i = 0;i < triMeshes.size();i++) {
			// Per-mesh domain palette from the owning actor face material.
			// domainColorTex already maps every domain to its solid color (a solid
			// owns several domains, all sharing the same color), so reusing it here
			// gives the path tracer the same per-solid coloring as the raster view.
			std::vector<Maths::FVector4> meshDomainPalette;
			// The mesh instance actor is the face mesh owner (e.g. the "AllFaces"
			// child of a CTopoShape); its material carries the per-domain solid
			// palette. Walk up to the parent if the actor itself has no material.
			for (auto& inst : triMeshInstances)
			{
				if (inst.meshID != i) continue;
				auto actor = scene->FindActorByID(inst.actorID);
				if (!actor) break;
				auto matList = actor->GetComponent<::Core::ECS::Components::CMaterialRenderer>();
				if (!matList && actor->GetParent())
				{
					matList = actor->GetParent()->GetComponent<::Core::ECS::Components::CMaterialRenderer>();
				}
				if (!matList) break;
				auto mat = matList->GetMaterialAtIndex(0);
				if (!mat) break;
				auto prop = mat->GetProperty("domainColorTex");
				if (!prop.has_value()) break;
				auto handle = std::get_if<::Rendering::HAL::TextureHandle*>(&prop.value().value);
				if (!handle || !*handle) break;
				auto* tex = static_cast<::Rendering::HAL::GLTexture*>(*handle);
				glBindTexture(GL_TEXTURE_BUFFER, tex->GetID());
				GLint texSize = 0;
				glGetTexLevelParameteriv(GL_TEXTURE_BUFFER, 0, GL_TEXTURE_BUFFER_SIZE, &texSize);
				int colorCount = texSize / static_cast<int>(sizeof(Maths::FVector4));
				if (colorCount > 0)
				{
					meshDomainPalette.resize(colorCount);
					// glGetTexImage is invalid for texture buffers: read the
					// underlying GL buffer object instead.
					GLint bufferId = 0;
					glGetTexLevelParameteriv(GL_TEXTURE_BUFFER, 0, GL_TEXTURE_BUFFER_DATA_STORE_BINDING, &bufferId);
					if (bufferId != 0)
					{
						glBindBuffer(GL_TEXTURE_BUFFER, bufferId);
						glGetBufferSubData(GL_TEXTURE_BUFFER, 0, texSize, meshDomainPalette.data());
						glBindBuffer(GL_TEXTURE_BUFFER, 0);
					}
				}
				glBindTexture(GL_TEXTURE_BUFFER, 0);
			}
			if (triMeshes[i]->GetBvh() == nullptr) {
				continue;
			}
			int numIndices = triMeshes[i]->GetBvh()->GetNumIndices();
			const int* triIndices = triMeshes[i]->GetBvh()->GetIndices();
			auto& indexArr = triMeshes[i]->GetIndices();
			bool isIndexMesh = indexArr.size() > 0;
			auto& vertexData = triMeshes[i]->GetVerticesBVH();
			// The three arrays have to agree with each other: the BVH counts the
			// triangles of the mesh, and every triangle reads three entries out of the
			// index array - or, for a mesh without one, out of the vertices. A mesh that
			// was built half way (or whose indices were never uploaded) would be read
			// past its end below, which is a crash rather than a missing triangle.
			const int indexCount = static_cast<int>(indexArr.size());
			const int vertexCount = static_cast<int>(vertexData.size());
			if (triIndices == nullptr) {
				numIndices = 0;
			}
			else if (isIndexMesh && indexCount < numIndices * 3) {
				buildProblems.push_back(
					"mesh " + std::to_string(i) + " is indexed by " + std::to_string(numIndices)
					+ " triangle(s) but holds only " + std::to_string(indexCount)
					+ " index(es); the copy stops where they end"
				);
				numIndices = indexCount / 3;
			}
			else if (!isIndexMesh && vertexCount < numIndices * 3) {
				buildProblems.push_back(
					"mesh " + std::to_string(i) + " is indexed by " + std::to_string(numIndices)
					+ " triangle(s) but holds only " + std::to_string(vertexCount)
					+ " vertex(es); the copy stops where they end"
				);
				numIndices = vertexCount / 3;
			}
			for (int j = 0; j < numIndices; j++)
			{
				int index = triIndices[j];
				if (index < 0
					|| (isIndexMesh ? (index * 3 + 2) >= indexCount
						: (index * 3 + 2) >= vertexCount)) {
					buildProblems.push_back(
						"mesh " + std::to_string(i) + " indexes triangle " + std::to_string(index)
						+ " outside its " + std::to_string(isIndexMesh ? indexCount : vertexCount)
						+ (isIndexMesh ? " index(es)" : " vertex(es)") + "; it is left out"
					);
					continue;
				}
				if (isIndexMesh) {
					int v1 = indexArr[(index * 3 + 0)] + verticesCnt;
					int v2 = indexArr[(index * 3 + 1)] + verticesCnt;
					int v3 = indexArr[(index * 3 + 2)] + verticesCnt;
					vertIndices.push_back(Indices{ v1, v2, v3 });
				}
				else
				{
					int v1 = (index * 3 + 0) + verticesCnt;
					int v2 = (index * 3 + 1) + verticesCnt;
					int v3 = (index * 3 + 2) + verticesCnt;
					vertIndices.push_back(Indices{ v1, v2, v3 });
				}
				// Per-triangle domain color (parallel to vertIndices): the three
				// vertices of a triangle always share the same domain.
				int localV0 = isIndexMesh ? indexArr[(index * 3 + 0)] : (index * 3 + 0);
				float domainIdxF = 0.0f;
				if (localV0 >= 0 && localV0 < vertexCount) {
					domainIdxF = vertexData[localV0].domainId.x;
				}
				else {
					buildProblems.push_back(
						"mesh " + std::to_string(i) + " points at vertex "
						+ std::to_string(localV0) + ", which its " + std::to_string(vertexCount)
						+ " vertex(es) do not contain; the triangle keeps the default colour"
					);
				}
				if (domainIdxF != domainIdxF) domainIdxF = 0.0f; // NaN guard
				int domainIdx = static_cast<int>(domainIdxF);
				triangleDomainColors.push_back(
					(domainIdx >= 0 && domainIdx < static_cast<int>(meshDomainPalette.size()))
						? meshDomainPalette[domainIdx]
						: DomainColorForTriangle(domainIdx));
			}
			for (int k = 0;k < vertexData.size();k++) {
				verticesUVX.push_back(Maths::FVector4(vertexData[k].position.x, vertexData[k].position.y, vertexData[k].position.z, vertexData[k].texCoords.x));
				normalsUVY.push_back(Maths::FVector4(vertexData[k].normals.x, vertexData[k].normals.y, vertexData[k].normals.z, vertexData[k].texCoords.y));
			}
			verticesCnt += vertexData.size();
		}
					// Copy transforms: instance transform and its inverse (both transposed),
		// so the shader can transform rays without a per-hit matrix inverse.
		transforms.resize(triMeshInstances.size() * 2);
		for (int i = 0; i < triMeshInstances.size(); i++)
		{
			transforms[i * 2 + 0] = Maths::FMatrix4::Transpose(triMeshInstances[i].transform);
			transforms[i * 2 + 1] = Maths::FMatrix4::Transpose(Maths::FMatrix4::Inverse(triMeshInstances[i].transform));
		}
		// Copy Textures
		int reqWidth = renderOptions.texArrayWidth;
		int reqHeight = renderOptions.texArrayHeight;
		int texBytes = reqWidth * reqHeight * 4;
		textureMapsArray.resize(texBytes * textures.size());
		for (int i = 0; i < textures.size(); i++)
		{
			int texWidth = textures[i]->GetWidth();
			int texHeight = textures[i]->GetHeight();
			// Resize textures to fit 2D texture array
			if (texWidth != reqWidth || texHeight != reqHeight)
			{
				unsigned char* resizedTex = new unsigned char[texBytes];
				stbir_resize_uint8(&textures[i]->texData[0], texWidth, texHeight, 0, resizedTex, reqWidth, reqHeight, 0, 4);
				std::copy(resizedTex, resizedTex + texBytes, &textureMapsArray[i * texBytes]);
				delete[] resizedTex;
			}
			else
				std::copy(textures[i]->texData.begin(), textures[i]->texData.end(), &textureMapsArray[i * texBytes]);
		}
		isDirty = true;
	}
	void BvhService::SaveAsObj(const std::string& path)
	{
		std::vector<Maths::FVector3>vertices;
		std::vector<unsigned int>indices;
		for (int i = 0; i < triMeshInstances.size(); i++) {
			int meshId = triMeshInstances[i].meshID;
			auto matrix = triMeshInstances[i].transform;
			auto& mesh = triMeshes[meshId];
			int numTrs = mesh->GetIndexCount() ? mesh->GetIndexCount() / 3 : mesh->GetVertexCount() / 3;
			for (int triIndex = 0; triIndex < numTrs; triIndex++) {
				Maths::FVector3 v0 = mesh->GetVertexPosition(triIndex * 3);
				Maths::FVector3 v1 = mesh->GetVertexPosition(triIndex * 3 + 1);
				Maths::FVector3 v2 = mesh->GetVertexPosition(triIndex * 3 + 2);
				vertices.push_back(Maths::FMatrix4::MulPoint(matrix, v0));
				vertices.push_back(Maths::FMatrix4::MulPoint(matrix, v1));
				vertices.push_back(Maths::FMatrix4::MulPoint(matrix, v2));
			}
		}
		indices.resize(vertices.size());
		for (int i = 0; i < indices.size(); i++) {
			indices[i] = i;
		}
		//exportObjOnlyVertices(path,(float*)vertices.data(),indices.data(),vertices.size(),indices.size());
	}
	void BvhService::Clear() {
		if (m_sceneBvh) {
			delete m_sceneBvh;
		}
		if (m_sceneTriBvh) {
			delete m_sceneTriBvh;
		}
		m_sceneBvh = new ::Rendering::Geometry::Bvh(10.0f, 64, false);
		m_sceneTriBvh = new ::Rendering::Geometry::Bvh(10.0f, 64, false);
		triMeshInstances.clear();
		triMeshes.clear();
		nodes.clear();
		bvhRootStartIndices.clear();
		materials.clear();
		lights.clear();
		topLevelBvh = nullptr;
		transforms.clear();
		textures.clear();
		textureMapsArray.clear();
		verticesUVX.clear();
		normalsUVY.clear();
		vertIndices.clear();
		triangleDomainColors.clear();
		triangleInfoMap.clear();
		curNode = 0;
		curTriIndex = 0;
		topLevelIndex = 0;
		// Collect all model renderers' bounds
	}
	bool BvhService::DirtyFlag()
	{
		return isDirty;
	}
	void BvhService::SetDirtyFlag(bool flag)
	{
		isDirty = flag;
	}
	void BvhService::AddTriangleInfo(int instanceId,const TriangleInfo& info)
	{
		for (int i = 0; i < triangleInfoMap[instanceId].size(); i++) {
			if ((triangleInfoMap[instanceId][i].info)== (info.info)) {
				return;
			}
		}
		triangleInfoMap[instanceId].push_back(info);
		isTriangleDirty = true;

		//batch triangleInfo
		int triOffset = 0;
		meshTriangleInfo.clear();
		triangleInfo.clear();
		std::vector<unsigned int>baseOffset(triMeshes.size(),0);
		meshTriangleInfo.resize(triMeshes.size());
		for (int i = 0; i < meshTriangleInfo.size(); i++) {
			meshTriangleInfo[i].start = -1;
			meshTriangleInfo[i].num = -1;
			baseOffset[i] = triOffset;
			triOffset+= triMeshes[i]->GetBvh()->GetNumIndices();
		}
		for (auto& it : triangleInfoMap) {
			if (it.second.size() > 0) {
				int mid = triMeshInstances[it.first].meshID;
				meshTriangleInfo[mid].start = triangleInfo.size();
				meshTriangleInfo[mid].num = it.second.size();
				meshTriangleInfo[mid].baseOffset = baseOffset[mid];
				triangleInfo.insert(triangleInfo.end(), it.second.begin(), it.second.end());
			}
		}
	}
	void BvhService::UpdateTriangleInfo()
	{
		if (isTriangleDirty) {
			for (auto& it : triangleInfoMap) {
				if (it.second.size() > 0) {
					auto instanceId = it.first;
					auto mid = triMeshInstances[instanceId].meshID;
					auto actorId = triMeshInstances[instanceId].actorID;
					auto actor = scene->FindActorByID(actorId);
					if (actor->HasComponent("CTopoShape")) {
						auto matList = actor->GetChild("AllFaces")->GetComponent<Core::ECS::Components::CMaterialRenderer>();
						if (matList) {
							auto mat = matList->GetMaterialAtIndex(0);
							if (mat) {
								mat->AddFeature("TRIANGLE_INFO");
								auto& tfo=it.second;
								::Rendering::HAL::GLTexture* triangleInfoTex = nullptr;	
								// TriangleInfo Texture
								::Rendering::Settings::TextureDesc desc;
								desc.isTextureBuffer = true;
								desc.internalFormat = ::Rendering::Settings::EInternalFormat::RG32UI;
								desc.buffetLen = tfo.size() * sizeof(::Core::SceneSystem::TriangleInfo);
								desc.mutableDesc = ::Rendering::Settings::MutableTextureDesc{
									.data = tfo.data()
								};
								int ss = tfo.size();
								mat->SetProperty("trifoLen",ss);
								const ::Rendering::Data::MaterialProperty prop=mat->GetProperty("triangleInfoTex").value();
								if (std::holds_alternative<::Rendering::Resources::Texture*>(prop.value)) {
									
									auto v = std::get<::Rendering::Resources::Texture*>(prop.value);

									if (v == nullptr) {

										triangleInfoTex = new ::Rendering::HAL::GLTexture(::Rendering::Settings::ETextureType::TEXTURE_BUFFER);
										triangleInfoTex->Allocate(desc);
										mat->SetProperty("triangleInfoTex", triangleInfoTex);
									}
									else
									{
										v->GetTexture().Allocate(desc);
									}
								}
								else
								{
									auto v = std::get<::Rendering::HAL::TextureHandle*>(prop.value);
									::Rendering::HAL::GLTexture* ttex = static_cast<::Rendering::HAL::GLTexture*>(v);
									ttex->Allocate(desc);
								}
							}
						}
					}
				}
			}
		}
	}
	bool BvhService::RayHit(const::Rendering::Geometry::Ray& ray, HitRes& outRes)
	{
		ZoneScoped;
		float triDist = 1e9;
		bool hit = false;
		int mid = -1;
		int tid = -1;
		int instanceId = -1;
		Maths::FVector3 hitNormal;
		Maths::FVector3 bary;
		std::vector<::Rendering::Geometry::Bvh::Node*>stack;
		if (m_sceneTriBvh != nullptr)
			stack.push_back(m_sceneTriBvh->m_root);
		while (!stack.empty()) {
			auto cur = stack.back();stack.pop_back();
			if (!cur)continue;
			float tempDist = 1e9;
			if (ray.HitDistance(cur->bounds, tempDist))
			{
				if (cur->type == ::Rendering::Geometry::Bvh::kInternal) {
					stack.push_back(cur->lc);
					stack.push_back(cur->rc);
				}
				else if (cur->type == ::Rendering::Geometry::Bvh::kLeaf) {
					for (int i = cur->startidx;i < cur->startidx + cur->numprims;i++) {
						int index = m_sceneTriBvh->m_packed_indices[i];
						int meshId = triMeshInstances[index].meshID;
						
						auto matrix = triMeshInstances[index].transform;
						auto invMatrix = Maths::FMatrix4::Inverse(matrix);
						auto localRay = ray.Transformed(invMatrix);
						auto& mesh = triMeshes[meshId];
						auto meshBvh = mesh->GetBvh();
						std::vector<::Rendering::Geometry::Bvh::Node*>meshBvhStack;
						meshBvhStack.push_back(meshBvh->m_root);
						while (!meshBvhStack.empty()) {
							auto meshBvhCur = meshBvhStack.back(); meshBvhStack.pop_back();
							if (!meshBvhCur)continue;
							float meshTempDist = 1e6;
							if (localRay.HitDistance(meshBvhCur->bounds, meshTempDist))
							{
								if (meshBvhCur->type == ::Rendering::Geometry::Bvh::kInternal) {
									meshBvhStack.push_back(meshBvhCur->lc);
									meshBvhStack.push_back(meshBvhCur->rc);
								}
								else if (meshBvhCur->type == ::Rendering::Geometry::Bvh::kLeaf) {
									for (int j = meshBvhCur->startidx;j < meshBvhCur->startidx + meshBvhCur->numprims;j++) {
										int triIndex = meshBvh->m_packed_indices[j];
										::Rendering::Geometry::VertexBVH v0 = mesh->GetVertexBVH(triIndex * 3);
										::Rendering::Geometry::VertexBVH v1 = mesh->GetVertexBVH(triIndex * 3 + 1);
										::Rendering::Geometry::VertexBVH v2 = mesh->GetVertexBVH(triIndex * 3 + 2);
										float currentTriDist = 1e6;
										Maths::FVector3 currentHitNormal;
										Maths::FVector3 currentBary;
										if (localRay.HitDistance(v0.position, v1.position, v2.position, currentTriDist, &currentHitNormal, &currentBary)) {
											if (currentTriDist < triDist) {
												triDist = currentTriDist;
												hitNormal = currentHitNormal;
												bary = currentBary;
												outRes.hitPoint = v0.position * bary[0] + v1.position * bary[1] + v2.position * bary[2];
												outRes.hitPoint = Maths::FMatrix4::MulPoint(matrix, outRes.hitPoint);
												outRes.hitNormal = Maths::FMatrix4::MulDir(matrix, hitNormal);
												outRes.triangleId = triIndex;
												outRes.actorId = triMeshInstances[index].actorID;
												outRes.hitUv = v0.texCoords * bary[0] + v1.texCoords * bary[1] + v2.texCoords * bary[2];
												
												hit = true;
												mid = meshId;
												instanceId = index;
												tid = triIndex;//j;
											}
										}
									}
								}
							}
						}
					}
				}
			}
		}
		//if (hit) {
		//	AddTriangleInfo(instanceId, TriangleInfo{ 255u << 24,static_cast<uint32_t>(tid) });
		//}
		return hit;
	}
	bool BvhService::ClipRayHit(const::Rendering::Geometry::Ray& ray, const Maths::FVector4& clipPlane, HitRes& outRes)
	{
		Maths::FVector3 nor(clipPlane.ToFVector3());
		float t0_ = nor.Dot((nor * clipPlane.w) - ray.origin_) /
			nor.Dot(ray.direction_);
		bool hitPlane= t0_ >= 0.0f;

		ZoneScoped;
		float triDist = 1e9;
		bool hit = false;
		int mid = -1;
		int tid = -1;
		int instanceId = -1;
		Maths::FVector3 hitNormal;
		Maths::FVector3 bary;
		std::vector<::Rendering::Geometry::Bvh::Node*>stack;
		if (m_sceneTriBvh != nullptr)
			stack.push_back(m_sceneTriBvh->m_root);
		while (!stack.empty()) {
			auto cur = stack.back();stack.pop_back();
			if (!cur)continue;
			float tempDist = 1e9;
			if (ray.HitDistance(cur->bounds, tempDist))
			{
				if (cur->type == ::Rendering::Geometry::Bvh::kInternal) {
					stack.push_back(cur->lc);
					stack.push_back(cur->rc);
				}
				else if (cur->type == ::Rendering::Geometry::Bvh::kLeaf) {
					for (int i = cur->startidx;i < cur->startidx + cur->numprims;i++) {
						int index = m_sceneTriBvh->m_packed_indices[i];
						int meshId = triMeshInstances[index].meshID;
						auto matrix = triMeshInstances[index].transform;
						auto invMatrix = Maths::FMatrix4::Inverse(matrix);
						auto localRay = ray.Transformed(invMatrix);
						auto& mesh = triMeshes[meshId];
						auto meshBvh = mesh->GetBvh();
						std::vector<::Rendering::Geometry::Bvh::Node*>meshBvhStack;
						meshBvhStack.push_back(meshBvh->m_root);
						while (!meshBvhStack.empty()) {
							auto meshBvhCur = meshBvhStack.back(); meshBvhStack.pop_back();
							if (!meshBvhCur)continue;
							float meshTempDist = 1e6;
							if (localRay.HitDistance(meshBvhCur->bounds, meshTempDist))
							{
								if (meshBvhCur->type == ::Rendering::Geometry::Bvh::kInternal) {
									meshBvhStack.push_back(meshBvhCur->lc);
									meshBvhStack.push_back(meshBvhCur->rc);
								}
								else if (meshBvhCur->type == ::Rendering::Geometry::Bvh::kLeaf) {
									for (int j = meshBvhCur->startidx;j < meshBvhCur->startidx + meshBvhCur->numprims;j++) {
										int triIndex = meshBvh->m_packed_indices[j];
										::Rendering::Geometry::VertexBVH v0 = mesh->GetVertexBVH(triIndex * 3);
										::Rendering::Geometry::VertexBVH v1 = mesh->GetVertexBVH(triIndex * 3 + 1);
										::Rendering::Geometry::VertexBVH v2 = mesh->GetVertexBVH(triIndex * 3 + 2);
										float currentTriDist = 1e6;
										Maths::FVector3 currentHitNormal;
										Maths::FVector3 currentBary;
										if (localRay.HitDistance(v0.position, v1.position, v2.position, currentTriDist, &currentHitNormal, &currentBary)) {
											Maths::FVector3 hitPoint = v0.position * currentBary[0] + v1.position * currentBary[1] + v2.position * currentBary[2];
											hitPoint = Maths::FMatrix4::MulPoint(matrix, hitPoint);
											if (hitPlane) {
												if (nor.Dot(hitPoint) - clipPlane.w > 0.0f) {
													continue;
												}
											}
											if (currentTriDist < triDist) {

												triDist = currentTriDist;
												hitNormal = currentHitNormal;
												bary = currentBary;
												outRes.hitPoint = hitPoint;
												outRes.hitNormal = Maths::FMatrix4::MulDir(matrix, hitNormal);
												outRes.triangleId = triIndex;
												outRes.actorId = triMeshInstances[index].actorID;
												outRes.hitUv = v0.texCoords * bary[0] + v1.texCoords * bary[1] + v2.texCoords * bary[2];

												hit = true;
												mid = meshId;
												instanceId = index;
												tid = triIndex;//j;
											}
										}
									}
								}
							}
						}
					}
				}
			}
		}
		return hit;
	}
	bool BvhService::RayIteratorHit(const::Rendering::Geometry::Ray& ray, HitRes& outRes)
	{
		ZoneScoped;
		float triDist = 1e6;
		bool hit = false;
		int mid = -1;
		int instanceId = -1;
		int tid = -1;
		Maths::FVector3 hitNormal;
		Maths::FVector3 bary;
		float tempDist = 1e6;

		if (ray.HitDistance(m_sceneTriBvh->Bounds(), tempDist)) {
			for (int i = 0;i < triMeshInstances.size();i++) {
				int meshId = triMeshInstances[i].meshID;
				auto matrix = triMeshInstances[i].transform;
				auto invMatrix = Maths::FMatrix4::Inverse(matrix);
				auto localRay = ray.Transformed(invMatrix);
				auto& mesh = triMeshes[meshId];
				int numTrs=mesh->GetIndexCount()? mesh->GetIndexCount()/3:mesh->GetVertexCount()/3;
				float meshTempDist = 1e6;
				if (localRay.HitDistance(mesh->GetBvh()->Bounds(), meshTempDist))
				{	
					
					for (int triIndex = 0;triIndex < numTrs;triIndex++) {
			
						Maths::FVector3 v0 = mesh->GetVertexPosition(triIndex * 3);
						Maths::FVector3 v1 = mesh->GetVertexPosition(triIndex * 3 + 1);
						Maths::FVector3 v2 = mesh->GetVertexPosition(triIndex * 3 + 2);
						float currentTriDist = 1e6;
						Maths::FVector3 currentHitNormal;
						Maths::FVector3 currentBary;
						if (localRay.HitDistance(v0, v1, v2, currentTriDist, &currentHitNormal, &currentBary)) {
							if (currentTriDist < triDist) {
								triDist = currentTriDist;
								hitNormal = currentHitNormal;
								bary = currentBary;
								outRes.hitPoint = v0 * bary[0] + v1 * bary[1] + v2 * bary[2];
								outRes.hitPoint = Maths::FMatrix4::MulPoint(matrix, outRes.hitPoint);
								outRes.hitNormal = Maths::FMatrix4::MulDir(matrix, hitNormal);
								outRes.triangleId = triIndex;
								outRes.actorId = triMeshInstances[i].actorID;
								outRes.hitUv = mesh->GetVertexBVH(triIndex * 3).texCoords;
								mid = meshId;
								tid = triIndex;
								instanceId = i;
								hit = true;
							}
						}		
					}
				}
			}	
		}
		//if (hit) {
		//	AddTriangleInfo(instanceId, TriangleInfo{ 255u << 24,static_cast<uint32_t>(tid) });
		//}
		return hit;
	}

	bool BvhService::PointPick(const Maths::FMatrix4& viewPortMatrix, int x, int y, float tolerance, PointPickRes& out)
	{
		ZoneScoped;
		bool isHit = false;
		for (int i = 0;i < mSceneMeshInstances.size();i++) {
			if (mSceneMeshInstances[i].actor->HasComponent("CBatchMeshLine")) {
				auto cbatchMesh = mSceneMeshInstances[i].actor->GetComponent<Core::ECS::Components::CBatchMeshLine>();
				isHit |= cbatchMesh->PointPick(viewPortMatrix, x, y, tolerance, out);
			}
		}
		return isHit;
	}

	std::vector<RectPickRes> BvhService::RectPick(const Maths::FMatrix4& viewProj, float su, float sv, float eu, float ev)
	{
		ZoneScoped;
		std::vector<RectPickRes> res;
		std::vector<::Rendering::Geometry::Bvh::Node*>stack;
		if (m_sceneTriBvh != nullptr)
			stack.push_back(m_sceneTriBvh->m_root);
		while (!stack.empty()) {
			auto cur = stack.back(); stack.pop_back();
			if (!cur)continue;
			auto tbox=cur->bounds.transform(viewProj);
			bool flag1 = tbox.pmin.x > eu || tbox.pmax.x < su;
			bool flag2 = tbox.pmin.y > ev || tbox.pmax.y < sv;
			if (!flag1 && !flag2) {
				if (cur->type == ::Rendering::Geometry::Bvh::kInternal) {
					stack.push_back(cur->lc);
					stack.push_back(cur->rc);
				}
				else if (cur->type == ::Rendering::Geometry::Bvh::kLeaf) {
					for (int i = cur->startidx; i < cur->startidx + cur->numprims; i++) {
						int index = m_sceneTriBvh->m_packed_indices[i];
						auto matrix = triMeshInstances[index].transform;
						if (triMeshInstances[index].actor->HasComponent("CBatchMeshTriangle")) {
							auto cbatchMesh= triMeshInstances[index].actor->GetComponent<Core::ECS::Components::CBatchMeshTriangle>();
							const auto& pickRes=cbatchMesh->RectPick(matrix,viewProj,su,sv,eu,ev);
							res.insert(res.end(),pickRes.begin(),pickRes.end());
						}
						else
						{	
							int meshId = triMeshInstances[index].meshID;
							auto& mesh = triMeshes[meshId];
							int actorId= triMeshInstances[index].actorID;
							auto meshBvh = mesh->GetBvh();
							std::vector<::Rendering::Geometry::Bvh::Node*>meshBvhStack;
							meshBvhStack.push_back(meshBvh->m_root);
							bool isHit = false;
							while (!meshBvhStack.empty()) {
								auto meshBvhCur = meshBvhStack.back(); meshBvhStack.pop_back();
								if (!meshBvhCur)continue;
								auto mbox=meshBvhCur->bounds.transform(matrix).transform(viewProj);
								if (!(mbox.pmin.x > eu || mbox.pmax.x < su) && !(mbox.pmin.y > ev || mbox.pmax.y < sv)) {
									if (meshBvhCur->type == ::Rendering::Geometry::Bvh::kInternal) {
										meshBvhStack.push_back(meshBvhCur->lc);
										meshBvhStack.push_back(meshBvhCur->rc);
									}
									else if (meshBvhCur->type == ::Rendering::Geometry::Bvh::kLeaf) {
										for (int j = meshBvhCur->startidx; j < meshBvhCur->startidx + meshBvhCur->numprims; j++) {
											int triIndex = meshBvh->m_packed_indices[j];
											::Rendering::Geometry::VertexBVH v0 = mesh->GetVertexBVH(triIndex * 3);
											::Rendering::Geometry::VertexBVH v1 = mesh->GetVertexBVH(triIndex * 3 + 1);
											::Rendering::Geometry::VertexBVH v2 = mesh->GetVertexBVH(triIndex * 3 + 2);
											Maths::FVector3 ndcV0 = viewProj.MulPoint(matrix.MulPoint(v0.position));
											Maths::FVector3 ndcV1 = viewProj.MulPoint(matrix.MulPoint(v1.position));
											Maths::FVector3 ndcV2 = viewProj.MulPoint(matrix.MulPoint(v2.position));
											//test intersection
											if (isTriangleAABBIntersect({ ndcV0.x,ndcV0.y }, { ndcV1.x,ndcV1.y },{ ndcV2.x,ndcV2.y }, su, sv, eu, ev)) {
												res.push_back({ actorId,(int)v0.texCoords.x,triIndex });
												isHit = true;
												break;
											}
										}
									}
								}
								if (isHit) {
									break;
								}
							}
						}
					}
				}
			}
		}
		return res;
	}
	BvhService::~BvhService() {
		delete m_sceneBvh;
		delete m_sceneTriBvh;
	}
}
