#pragma once
#include <vector>
#include <Core/ECS/Components/AComponent.h>
#include <Maths/FVector3.h>
namespace Core::ECS { class Actor; }
namespace Core::ECS::Components
{
	/** The topology vertices of one actor, batched into a single point mesh.
	 *
	 * It is the counterpart of CBatchMeshLine for vertices: the mesh holds one
	 * point per vertex of the shape, so a body is one draw call, and the point
	 * carries the vertex id the way a line carries the id of its edge - that id is
	 * what a pick reports and what a highlight colours. The component marks the
	 * actor as such a batch, which is what the picking pass reads it for.
	 */
	class CBatchMeshPoint : public AComponent
	{
	public:
		CBatchMeshPoint(ECS::Actor& p_owner);
		virtual ~CBatchMeshPoint()override;
		std::string GetName() override;
		virtual void OnUpdate(float p_deltaTime) override;
		/** Position of the vertex with this id, in the actor's own space. */
		Maths::FVector3 getPoint(int index);
		/** How many vertices the batch holds. */
		int GetPointCount();
		virtual void OnSerialize(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node) override;
		virtual void OnDeserialize(tinyxml2::XMLDocument& p_doc, tinyxml2::XMLNode* p_node) override;
	private:
		class CBatchMeshPointInternal;
		CBatchMeshPointInternal* mInternal = nullptr;
	};
}
