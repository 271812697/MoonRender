#pragma once
#include <Rendering/Data/PipelineState.h>
#include <Rendering/Data/FrameDescriptor.h>
#include <Rendering/Settings/ERenderPassOrder.h>

namespace Rendering::Core
{
	class CompositeRenderer;
}

namespace Rendering::Core
{
	class ARenderPass
	{
	public:
		ARenderPass(Core::CompositeRenderer& p_renderer);
		virtual ~ARenderPass() = default;
		virtual void ResizeRenderer(int width,int height);
		void SetEnabled(bool p_enabled);
		bool IsEnabled() const;
	protected:

		virtual void OnBeginFrame(const Data::FrameDescriptor& p_frameDescriptor);
		virtual void OnEndFrame();
		virtual void Draw(Rendering::Data::PipelineState p_pso) = 0;
	protected:
		Core::CompositeRenderer& m_renderer;
		bool m_enabled = true;
		friend class Core::CompositeRenderer;
	};
}