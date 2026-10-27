#include <tinyxml2.h>
#include <Core/ECS/Actor.h>
#include <Core/ECS/Components/CBatchMeshTriangle.h>
#include <Core/ECS/Components/CModelRenderer.h>
#include <Core/Global/ServiceLocator.h>
#include <Core/ResourceManagement/MaterialManager.h>
#include <Core/ECS/Components/CMaterialRenderer.h>
#include<Core/ECS/Components/CModelRenderer.h>
#include <Rendering/Geometry/bvh.h>
#include <Rendering/Geometry/split_bvh.h>
#include <thread>
namespace Core::ECS::Components
{
	class CBatchMeshTriangle::CBatchMeshTriangleInternal {
	public:
		CBatchMeshTriangleInternal(CBatchMeshTriangle* self) :mSelf(self){

		}
		~CBatchMeshTriangleInternal() {
			delete rootBvh;
			for (int i = 0; i < subMeshBvhs.size(); i++) {
				delete subMeshBvhs[i];
			}
		}
	private:
		friend class CBatchMeshTriangle;
		CBatchMeshTriangle* mSelf = nullptr;
		bool colorChange = false;
		bool candidatesChange = false;
		int hoverIndex =-1;
		std::vector<int> candidatesIndex;
		Maths::FVector4 candidateColor;
		Maths::FVector4 hoverColor;

		std::vector<Maths::FVector4> m_defaultColors;
		::Rendering::Geometry::Bvh* rootBvh = nullptr;
		std::vector<::Rendering::Geometry::SplitBvh*>subMeshBvhs;
		std::vector<uint32_t> subMeshRanges;
	};
	CBatchMeshTriangle::CBatchMeshTriangle(ECS::Actor& p_owner) : AComponent(p_owner),mInternal(new CBatchMeshTriangleInternal(this))
	{
	}

	CBatchMeshTriangle::~CBatchMeshTriangle()
	{
		delete mInternal;
	}

	std::string CBatchMeshTriangle::GetName()
	{
		return "CBatchMeshTriangle";
	}

	void CBatchMeshTriangle::OnUpdate(float p_deltaTime)
	{
		if (mInternal->colorChange) {
			mInternal->colorChange = false;
			auto mat = owner.GetComponent<CMaterialRenderer>()->GetMaterialAtIndex(0);
			// The slot may hold a material that has no domain colour table at all (the user can
			// assign any .mat to the faces); .value() on the empty optional used to throw here,
			// which took the editor down the moment a face was picked.
			const auto property = mat != nullptr ? mat->GetProperty("domainColorTex")
				: Tools::Utils::OptRef<const ::Rendering::Data::MaterialProperty>{};
			const auto* texture = property ? std::get_if<::Rendering::HAL::TextureHandle*>(&property.value().value) : nullptr;
			if (texture != nullptr) {
				auto tex = *texture;

				if (tex) {
					std::vector<Maths::FVector4> colors = mInternal->m_defaultColors;
	
					for (auto& idx : mInternal->candidatesIndex) {
						colors[idx] = mInternal->candidateColor;
					}
					if (mInternal->hoverIndex != -1) {
						colors[mInternal->hoverIndex] = mInternal->hoverColor;
					}
					::Rendering::Settings::TextureDesc desc;
					desc.isTextureBuffer = true;
					desc.internalFormat = ::Rendering::Settings::EInternalFormat::RGBA32F;
					desc.buffetLen = colors.size() * sizeof(Maths::FVector4);
					desc.mutableDesc = ::Rendering::Settings::MutableTextureDesc{
						.data = colors.data()
					};
					static_cast<::Rendering::HAL::GLTexture*>(tex)->Allocate(desc);
				}
			}
		}
		if (mInternal->candidatesChange) {
			mInternal->candidatesChange = false;
		}
	}

	void CBatchMeshTriangle::SetColors(const std::vector<Maths::FVector4>& colors)
	{
		mInternal->m_defaultColors = colors;
	}

	void CBatchMeshTriangle::SetColor( const Maths::FVector4& color)
	{
		//mInternal->candidatesIndex.clear();;
	
		mInternal->colorChange = true;
		mInternal->candidateColor = color;
		//for (int i = 0; i < index.size(); i++) {
		//	int idx = index[i];
		//	if (idx >= 0 && idx < mInternal->m_defaultColors.size()) {
		//		mInternal->colorChange = true;
		//		mInternal->candidatesIndex.push_back(index);
		//	}
		//}
	}

	void CBatchMeshTriangle::SetCandidatesIndex(const std::vector<int>& index)
	{
		mInternal->candidatesChange = true;
		mInternal->candidatesIndex = index;
	}

	void CBatchMeshTriangle::SetHoverColor(int index, const Maths::FVector4& color)
	{
		mInternal->hoverIndex = index;
		mInternal->hoverColor = color;
		mInternal->colorChange = true;
	}

	void CBatchMeshTriangle::ClearHoverColor()
	{
		mInternal->colorChange = true;
		mInternal->hoverIndex = -1;
	}

	void CBatchMeshTriangle::BuildBvh(const std::vector<::Rendering::Geometry::bbox>& boxs, const std::vector<uint32_t>& subMeshRanges)
	{
		return;
		//std::thread* th=new std::thread([this,boxs,subMeshRanges]() {
		//	
		//if (mInternal->rootBvh) {
		//	delete mInternal->rootBvh;
		//}
		//if (mInternal->subMeshBvhs.size()) {
		//	for (int i = 0; i < mInternal->subMeshBvhs.size(); i++) {
		//		if(mInternal->subMeshBvhs[i])
		//		delete mInternal->subMeshBvhs[i];
		//	}
		//	mInternal->subMeshBvhs.clear();
		//}

		//mInternal->rootBvh = new ::Rendering::Geometry::Bvh(10.0f, 64, false);
		//mInternal->rootBvh->Build(boxs.data(),boxs.size());//::Rendering::Geometry::Bvh rootBvh;
		//auto model=owner.GetComponent<Core::ECS::Components::CModelRenderer>();
		//auto mesh=model->GetModel()->GetMeshes()[0];
		//auto& indices =mesh->GetIndices();
		//auto& vertex=mesh->GetVerticesBVH();
		//uint32_t ioffset = 0;
		//for (int i = 0; i < subMeshRanges.size(); i++) {
		//	int numTris = (subMeshRanges[i] - ioffset) / 3;
		//	auto bvh = new ::Rendering::Geometry::SplitBvh(2.0f, 64, 0, 0.001f, 0);
		//	//为所有的三角形构建包围盒，然后在对所有的包围盒构建bvh
		//	std::vector<::Rendering::Geometry::bbox> bounds(numTris);
		//	for (int k = 0; k < numTris; k++)
		//	{

		//		bounds[k].grow(vertex[indices[3 * k+ ioffset]].position);
		//		bounds[k].grow(vertex[indices[3 * k + 1 + ioffset]].position);
		//		bounds[k].grow(vertex[indices[3 * k + 2 + ioffset]].position);
		//	}
		//	bvh->Build(&bounds[0], numTris);
		//	mInternal->subMeshBvhs.push_back(bvh);
		//	ioffset = subMeshRanges[i];
		//}
		//mInternal->subMeshRanges.resize(subMeshRanges.size());
		//for (int i = 0; i < subMeshRanges.size(); i++) {
		//	if (i == 0) {
		//		mInternal->subMeshRanges[i] = 0;
		//	}
		//	else
		//	{
		//		mInternal->subMeshRanges[i] = subMeshRanges[i-1];
		//	}
		//}			
		//	
		//	});
		//
		//
	
	}
	std::vector<Core::SceneSystem::RectPickRes> CBatchMeshTriangle::RectPick(const Maths::FMatrix4& modelMatrix,const Maths::FMatrix4& viewProj, float su, float sv, float eu, float ev) {
		//auto model = owner.GetComponent<Core::ECS::Components::CModelRenderer>();
		//int actorId = owner.GetID();
		//auto mesh = model->GetModel()->GetMeshes()[0];
		//auto& indices = mesh->GetIndices();
		//auto& vertex = mesh->GetVerticesBVH();
		//std::vector<Core::SceneSystem::RectPickRes>res;
		//std::vector<::Rendering::Geometry::Bvh::Node*>stack;
	 //   stack.push_back(mInternal->rootBvh->m_root);
		//while (!stack.empty()) {
		//	auto cur = stack.back(); stack.pop_back();
		//	if (!cur)continue;
		//	auto tbox = cur->bounds.transform(modelMatrix).transform(viewProj);
		//	bool flag1 = tbox.pmin.x > eu || tbox.pmax.x < su;
		//	bool flag2 = tbox.pmin.y > ev || tbox.pmax.y < sv;
		//	if (!flag1 && !flag2) {
		//		if (cur->type == ::Rendering::Geometry::Bvh::kInternal) {
		//			stack.push_back(cur->lc);
		//			stack.push_back(cur->rc);
		//		}
		//		else if (cur->type == ::Rendering::Geometry::Bvh::kLeaf) {
		//			for (int i = cur->startidx; i < cur->startidx + cur->numprims; i++) {
		//				int index = mInternal->rootBvh->m_packed_indices[i];
		//				uint32_t ioffset = mInternal->subMeshRanges[index];
		//				auto meshBvh=mInternal->subMeshBvhs[index];
		//				std::vector<::Rendering::Geometry::Bvh::Node*>meshBvhStack;
		//				meshBvhStack.push_back(meshBvh->m_root);
		//				bool isHit = false;
		//				while (!meshBvhStack.empty()) {
		//					auto meshBvhCur = meshBvhStack.back(); meshBvhStack.pop_back();
		//					if (!meshBvhCur)continue;
		//					auto mbox = meshBvhCur->bounds.transform(modelMatrix).transform(viewProj);
		//					if (!(mbox.pmin.x > eu || mbox.pmax.x < su) && !(mbox.pmin.y > ev || mbox.pmax.y < sv)) {
		//						if (meshBvhCur->type == ::Rendering::Geometry::Bvh::kInternal) {
		//							meshBvhStack.push_back(meshBvhCur->lc);
		//							meshBvhStack.push_back(meshBvhCur->rc);
		//						}
		//						else if (meshBvhCur->type == ::Rendering::Geometry::Bvh::kLeaf) {
		//							for (int j = meshBvhCur->startidx; j < meshBvhCur->startidx + meshBvhCur->numprims; j++) {
		//								int triIndex = meshBvh->m_packed_indices[j];
		//								::Rendering::Geometry::VertexBVH v0 = vertex[indices[triIndex * 3+ioffset]];
		//								::Rendering::Geometry::VertexBVH v1 = vertex[indices[triIndex * 3 + 1+ioffset]];
		//								::Rendering::Geometry::VertexBVH v2 = vertex[indices[triIndex * 3 + 2 + ioffset]];
		//								Maths::FVector3 ndcV0 = viewProj.MulPoint(modelMatrix.MulPoint(v0.position));
		//								Maths::FVector3 ndcV1 = viewProj.MulPoint(modelMatrix.MulPoint(v1.position));
		//								Maths::FVector3 ndcV2 = viewProj.MulPoint(modelMatrix.MulPoint(v2.position));
		//								//test intersection
		//								if (::Core::SceneSystem::isTriangleAABBIntersect({ ndcV0.x,ndcV0.y }, { ndcV1.x,ndcV1.y }, { ndcV2.x,ndcV2.y }, su, sv, eu, ev)) {
		//									res.push_back({ actorId,(int)v0.texCoords.x,triIndex });
		//									isHit = true;
		//									break;
		//								}
		//								
		//							}
		//						}
		//					}
		//					if (isHit) {
		//						break;
		//					}
		//				}
		//			}
		//		}
		//	
		//	}
		//}
		//return res;
		return std::vector<Core::SceneSystem::RectPickRes>{};
	}

	void CBatchMeshTriangle::OnSerialize(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node)
	{
	}

	void CBatchMeshTriangle::OnDeserialize(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node)
	{
	}
}
