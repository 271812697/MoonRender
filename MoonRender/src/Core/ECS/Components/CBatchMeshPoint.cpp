#include <tinyxml2.h>
#include <Core/ECS/Actor.h>
#include <Core/ECS/Components/CBatchMeshPoint.h>
#include <Core/ECS/Components/CModelRenderer.h>

namespace Core::ECS::Components
{
	class CBatchMeshPoint::CBatchMeshPointInternal {
	public:
		CBatchMeshPointInternal(CBatchMeshPoint* self) :mSelf(self) {

		}
		~CBatchMeshPointInternal() {
		}
	private:
		friend class CBatchMeshPoint;
		CBatchMeshPoint* mSelf = nullptr;
	};

	CBatchMeshPoint::CBatchMeshPoint(ECS::Actor& p_owner) : AComponent(p_owner), mInternal(new CBatchMeshPointInternal(this))
	{
	}

	CBatchMeshPoint::~CBatchMeshPoint()
	{
		delete mInternal;
	}

	std::string CBatchMeshPoint::GetName()
	{
		return "CBatchMeshPoint";
	}

	void CBatchMeshPoint::OnUpdate(float p_deltaTime)
	{
	}

	Maths::FVector3 CBatchMeshPoint::getPoint(int index)
	{
		auto* modelRenderer = owner.GetComponent<Core::ECS::Components::CModelRenderer>();
		if (modelRenderer == nullptr) {
			return {};
		}
		auto* model = modelRenderer->GetModel();
		if (model == nullptr) {
			return {};
		}
		const auto& meshes = model->GetMeshes();
		if (meshes.empty() || meshes[0] == nullptr) {
			return {};
		}
		// One index per point, so the point's id is the index that draws it.
		const auto& indices = meshes[0]->GetIndices();
		if (index < 0 || index >= static_cast<int>(indices.size())) {
			return {};
		}
		auto& vertices = meshes[0]->GetVerticesBVH();
		const uint32_t vertexIndex = indices[index];
		if (vertexIndex >= vertices.size()) {
			return {};
		}
		return vertices[vertexIndex].position;
	}

	int CBatchMeshPoint::GetPointCount()
	{
		auto* modelRenderer = owner.GetComponent<Core::ECS::Components::CModelRenderer>();
		if (modelRenderer == nullptr) {
			return 0;
		}
		auto* model = modelRenderer->GetModel();
		if (model == nullptr) {
			return 0;
		}
		const auto& meshes = model->GetMeshes();
		if (meshes.empty() || meshes[0] == nullptr) {
			return 0;
		}
		return static_cast<int>(meshes[0]->GetIndices().size());
	}

	void CBatchMeshPoint::OnSerialize(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node)
	{
	}

	void CBatchMeshPoint::OnDeserialize(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node)
	{
	}
}
