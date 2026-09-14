#include <ranges>
#include <Core/ECS/Components/CMaterialRenderer.h>
#include <Core/Rendering/EngineDrawableDescriptor.h>

#include "Core/Global/ServiceLocator.h"
#include "PointRenderPass.h"
#include "renderer/DebugSceneRenderer.h"
#include <glad/glad.h>
static ::Rendering::Resources::Model* sphere = nullptr;

Editor::Rendering::PointRenderPass::PointRenderPass(::Rendering::Core::CompositeRenderer& p_renderer)
	: ::Rendering::Core::ARenderPass(p_renderer)
{
	m_PointMaterial.SetShader(::Core::Global::ServiceLocator::Get<Editor::Core::Context>().shaderManager[":Shaders\\Point.ovfx"]);
	m_PointMaterial.SetDepthTest(true);
	sphere= ::Core::Global::ServiceLocator::Get<Editor::Core::Context>().modelManager[":Models/Sphere.fbx"];

}

void Editor::Rendering::PointRenderPass::Draw(::Rendering::Data::PipelineState p_pso)
{

	auto& debugSceneDescriptor = m_renderer.GetDescriptor<Editor::Rendering::DebugSceneRenderer::DebugSceneDescriptor>();
	//m_renderer.Clear(false, true, false);
	if (debugSceneDescriptor.selectedActor)
	{
		auto& selectedActor = debugSceneDescriptor.selectedActor.value();
		if (auto selectModel = selectedActor.GetComponent<::Core::ECS::Components::CModelRenderer>(); selectModel) {
			auto engineDrawableDescriptor = ::Core::Rendering::EngineDrawableDescriptor{
			selectedActor.transform.GetWorldMatrix(),
	        Maths::FMatrix4::Identity
			};
			//Maths::FMatrix4::Scale(selectedActor.transform.GetWorldMatrix(),{0.1,0.1,0.1});
			//selectMode.
			
			auto selectMesh=selectModel->GetModel()->GetMeshes()[0];
			auto& sphereVao=sphere->GetMeshes()[0]->getVertexArray();
			auto& instanceVBO = selectMesh->getVertexBuffer();
			
			sphereVao.Bind();
			instanceVBO.Bind();
			// Instance attribute 5 = the position of each vertex of the selected
			// mesh. The stride has to come from the mesh: a model loaded from a file
			// uses the 14 float Vertex layout, while a STEP/topology batch uses the
			// 10 float VertexBVH one, and reading either with the other's stride
			// lands the points between vertices.
			glEnableVertexAttribArray(5);
			glVertexAttribPointer(
				5, 3, GL_FLOAT, GL_FALSE, selectMesh->GetVertexStride(), (void*)0);
			glVertexAttribDivisor(5, 1);  // 每个实例更新一次
			sphereVao.Unbind();
			instanceVBO.Unbind();
			m_PointMaterial.SetGPUInstances(selectMesh->GetVertexCount());
			auto stateMask = m_PointMaterial.GenerateStateMask();
			::Rendering::Entities::Drawable element;
			
			element.mesh = *sphere->GetMeshes()[0];
			element.material = m_PointMaterial;
			element.stateMask = stateMask;
			element.AddDescriptor(engineDrawableDescriptor);
			m_renderer.DrawEntity(p_pso, element);
		}
	}
}
