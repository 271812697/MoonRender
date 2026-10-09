#pragma once
#include <string>
#include <Rendering/Geometry/bvh.h>
#include <Rendering/HAL/Texture.h>
#include <Core/ECS/Actor.h>
#include <Core/SceneSystem/Intersection.h>
#include <Rendering/Geometry/ray.h>
namespace Rendering::Resources {
	class Mesh;
}
namespace Core::Resources {
	class Material;
}
namespace Core::SceneSystem
{
	class Scene;
	enum AlphaMode
	{
		Opaque,
		Blend,
		Mask
	};

	enum MediumType
	{
		None,
		Absorb,
		Scatter,
		Emissive
	};
	struct MeshInstance
	{

		MeshInstance(int64_t aci, ::Core::ECS::Actor* ac,int meshId, const Maths::FMatrix4& xform, int matId)
			: actorID(aci)
			, meshID(meshId)
			, transform(xform), localform(xform)
			, materialID(matId),
			actor(ac)
		{
			parentID = -1;
		}
		~MeshInstance() {}

		Maths::FMatrix4 transform;
		Maths::FMatrix4 localform;
		int64_t	actorID;

		::Core::ECS::Actor* actor = nullptr;
		int materialID;
		int meshID;
		int parentID;
	};
	struct Indices
	{
		int x, y, z;
	};
	class Material
	{
	public:
		Material()
		{
			baseColor = Maths::FVector3(1.0f, 1.0f, 1.0f);
			//各向异性
			anisotropic = 0.0f;

			emission = Maths::FVector3(0.0f, 0.0f, 0.0f);
			// padding1

			metallic = 0.0f;
			roughness = 0.5f;
			subsurface = 0.0f;
			specularTint = 0.0f;

			sheen = 0.0f;
			sheenTint = 0.0f;
			clearcoat = 0.0f;
			clearcoatGloss = 0.0f;

			specTrans = 0.0f;
			ior = 1.5f;
			//介质类型
			mediumType = 0.0f;
			mediumDensity = 0.0f;

			mediumColor = Maths::FVector3(1.0f, 1.0f, 1.0f);
			//各向同性
			mediumAnisotropy = 0.0f;

			baseColorTexId = -1.0f;
			metallicRoughnessTexID = -1.0f;
			normalmapTexID = -1.0f;
			emissionmapTexID = -1.0f;

			opacity = 1.0f;
			alphaMode = 0.0f;
			alphaCutoff = 0.0f;
			// padding2
		};

		Maths::FVector3 baseColor;
		float anisotropic;

		Maths::FVector3 emission;
		float ax;

		float metallic;
		float roughness;
		float subsurface;
		float specularTint;

		float sheen;
		float sheenTint;
		float clearcoat;
		float clearcoatGloss;

		float specTrans;
		float ior;
		float mediumType;
		float mediumDensity;

		Maths::FVector3 mediumColor;
		float mediumAnisotropy;

		float baseColorTexId;
		float metallicRoughnessTexID;
		float normalmapTexID;
		float emissionmapTexID;

		float opacity;
		float alphaMode;
		float alphaCutoff;
		float ay;
	};
	enum LightType
	{
		RectLight,
		SphereLight,
		DistantLight
	};

	struct Light
	{
		Maths::FVector3 position;
		Maths::FVector3 emission;
		Maths::FVector3 u;
		Maths::FVector3 v;
		float radius;
		float area;
		float type;
	};
	struct TriangleInfo {
		uint32_t color=0;
		uint32_t info=0;//tid
		
	};
	struct MeshTriangleInfo
	{
		int start=-1;
		int num=-1;
		int baseOffset = -1;
	};
	struct RenderOptions
	{
		RenderOptions()
		{
			renderResolution = Maths::FVector2(1280, 720);
			windowResolution = Maths::FVector2(1280, 720);
			uniformLightCol = Maths::FVector3(0.3f, 0.3f, 0.3f);
			backgroundCol = Maths::FVector3(1.0f, 1.0f, 1.0f);
			tileWidth = 100;
			tileHeight = 100;
			maxDepth = 8;
			maxSpp = -1;
			RRDepth = 3;
			texArrayWidth = 512;
			texArrayHeight = 512;
			denoiserFrameCnt = 20;
			enableRR = true;
			enableDenoiser =true;
			enableTonemap = true;
			enableAces = false;
			openglNormalMap = true;
			enableEnvMap = true;
			enableUniformLight = false;
			hideEmitters = false;
			enableBackground = false;
			transparentBackground = false;
			independentRenderSize = false;
			enableRoughnessMollification = false;
			enableVolumeMIS = false;
			optLight = false;
			optAlphaTest = false;
			optMedium = false;


			envMapIntensity = 1.0f;
			envMapRot = 0.0f;
			roughnessMollificationAmt = 0.0f;
		}

		Maths::FVector2 renderResolution;
		Maths::FVector2 windowResolution;
		Maths::FVector3 uniformLightCol;
		Maths::FVector3 backgroundCol;
		int tileWidth;
		int tileHeight;
		int maxDepth;
		int maxSpp;
		int RRDepth;
		int texArrayWidth;
		int texArrayHeight;
		int denoiserFrameCnt;
		bool optLight;
		bool optAlphaTest;
		bool optMedium;
		bool enableRR;
		bool enableDenoiser;
		bool enableTonemap;
		bool enableAces;
		bool simpleAcesFit;
		bool openglNormalMap;
		bool enableEnvMap;
		bool enableUniformLight;
		bool hideEmitters;
		bool enableBackground;
		bool transparentBackground;
		bool independentRenderSize;
		bool enableRoughnessMollification;
		bool enableVolumeMIS;
		float envMapIntensity;
		float envMapRot;
		float roughnessMollificationAmt;
	};

	class BvhService {
	public:
		BvhService(Scene* sc) ;
		struct Node
		{
			Maths::FVector3 bboxmin;
			Maths::FVector3 bboxmax;
			Maths::FVector3 LRLeaf;
		};
		void AddMaterial(::Core::Resources::Material* material);
		int AddTexture(::Rendering::HAL::Texture*);
		void ProcessBLAS();
		void ProcessTLAS();
		int ProcessBLASNodes(const ::Rendering::Geometry::Bvh::Node* node);
		int ProcessTLASNodes(const ::Rendering::Geometry::Bvh::Node* node);

		void UpdateTLAS(const ::Rendering::Geometry::Bvh* topLevelBvh, const std::vector<MeshInstance>& instances);
		void Process(const std::vector<::Rendering::Geometry::bbox>&boxs, const std::vector<::Rendering::Resources::Mesh*>& sceneMeshes, const std::vector<MeshInstance>& instances);
		void SaveAsObj(const std::string&path);
		void Clear();
		bool DirtyFlag();
		void SetDirtyFlag(bool flag);
		void AddTriangleInfo(int mid,const TriangleInfo& info);
		void UpdateTriangleInfo();
		bool RayHit(const ::Rendering::Geometry::Ray& ray, HitRes& outRes);
		bool ClipRayHit(const ::Rendering::Geometry::Ray& ray, const Maths::FVector4& clipPlane,HitRes& outRes);
		bool RayIteratorHit(const ::Rendering::Geometry::Ray& ray, HitRes& outRes);
		bool PointPick(const Maths::FMatrix4& viewProj,int x,int y,float tolerance,PointPickRes& out);
		std::vector<RectPickRes> RectPick(const Maths::FMatrix4& viewProj, float su, float sv,float eu,float ev);
		~BvhService();
	private:
		Scene* scene;
	public:
		bool isMaterialDirty = false;
		bool isDirty = false;
		bool isTriangleDirty = false;
		friend class Scene;
		RenderOptions renderOptions;
		::Rendering::Geometry::Bvh* m_sceneBvh = nullptr;
		::Rendering::Geometry::Bvh* m_sceneTriBvh = nullptr;
		int topLevelIndex = 0;
		int nodeTexWidth;
		std::vector<Node> nodes;
		int curNode = 0;
		int curTriIndex = 0;
		std::vector<int> bvhRootStartIndices;
		std::vector<MeshInstance> triMeshInstances;
		std::vector<::Rendering::Resources::Mesh*> triMeshes;
		const ::Rendering::Geometry::Bvh* topLevelBvh;
		std::vector<MeshInstance> mSceneMeshInstances;
		std::vector<::Rendering::Resources::Mesh*> mSceneMeshes;
		/** What the last Process() could not index, one line each: an instance that
		 * names a mesh that is not in the scene, or a mesh whose index/vertex arrays
		 * disagree with the triangles its BVH holds. They are collected here instead of
		 * being logged, because the renderer has no logger of its own - the side that
		 * calls Process() reads them and says them (see SceneView::BuildBvh). */
		std::vector<std::string> buildProblems;
		// Scene Mesh Data 
		std::vector<Indices> vertIndices;
		std::vector<Maths::FVector4> verticesUVX; // Vertex + texture Coord (u/s)
		std::vector<Maths::FVector4> normalsUVY; // Normal + texture Coord (v/t)
		std::vector<Maths::FMatrix4> transforms;
		std::vector<TriangleInfo>triangleInfo;
		std::vector<Maths::FVector4> triangleDomainColors; // Per-triangle domain color, parallel to vertIndices
		std::vector<MeshTriangleInfo>meshTriangleInfo;
		std::unordered_map<int, std::vector<TriangleInfo>>triangleInfoMap;
		// Materials
		std::vector<Material> materials;
		// Lights
		std::vector<Light> lights;
		// textures
		std::vector<::Rendering::HAL::Texture*> textures;
		std::vector<unsigned char> textureMapsArray;
	};

}
