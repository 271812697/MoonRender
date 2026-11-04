#pragma once
#include <Core/ECS/Actor.h>

#include <Core/Resources/MaterialAsset.h>
#include <Core/Rendering/SceneRenderer.h>
#include <Core/SceneSystem/SceneManager.h>
#include <Rendering/HAL/VertexBuffer.h>
#include "Context.h"
namespace Editor::Rendering
{

	class PointRenderPass : public ::Rendering::Core::ARenderPass
	{
	public:
		PointRenderPass(::Rendering::Core::CompositeRenderer& p_renderer);
	private:
		virtual void Draw(::Rendering::Data::PipelineState p_pso) override;
	private:
		::Rendering::HAL::VertexBuffer vbo;
		::Core::Resources::MaterialAsset m_PointMaterial;
	};
}
