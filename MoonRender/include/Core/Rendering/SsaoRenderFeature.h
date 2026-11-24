#pragma once
#include <Rendering/Data/PipelineState.h>
#include <Rendering/Features/DebugShapeRenderFeature.h>
namespace Core::Rendering
{
	class SsaoRenderFeature : public ::Rendering::Features::ARenderFeature
	{
	public:
		SsaoRenderFeature(
			::Rendering::Core::CompositeRenderer& p_renderer,
			::Rendering::Features::EFeatureExecutionPolicy p_executionPolicy
		);

	
	protected:
		virtual void OnBeginFrame(const ::Rendering::Data::FrameDescriptor& p_frameDescriptor) override;
		virtual void OnBeforeDraw(::Rendering::Data::PipelineState& p_pso, const ::Rendering::Entities::Drawable& p_drawable);
	};
}
