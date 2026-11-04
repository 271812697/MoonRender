#pragma once
#include <Core/ECS/Actor.h>

#include <Core/Resources/MaterialAsset.h>
#include <Core/Rendering/SceneRenderer.h>
#include <Core/SceneSystem/SceneManager.h>
namespace MOON {
	class EventWidget;
}
namespace Editor::Rendering
{

	class GizmoRenderPass : public ::Rendering::Core::ARenderPass
	{
	public:
		GizmoRenderPass(::Rendering::Core::CompositeRenderer& p_renderer);
		~GizmoRenderPass();
		void enableGizmoWidget(const std::string& name,bool flag);
		bool isEnableGizmoWidget(const std::string&name);
		MOON::EventWidget* getGizmoWidget(const std::string& name);
		std::unordered_map<std::string, MOON::EventWidget*>& getGizmoWidgets();
	private:
		virtual void Draw(::Rendering::Data::PipelineState p_pso) override;
	private:
		class GizmoRenderPassInternal;
		GizmoRenderPassInternal* mInternal = nullptr;

	};
}
