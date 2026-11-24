#pragma once
#include <Rendering/Data/PipelineState.h>
#include <Rendering/Core/ARenderPass.h>

namespace Rendering::Core
{
	/** The pass that turns the frame into the image the window pastes.
	 *
	 * It exists so the present has the same context every other draw has: a pass object the feature
	 * hooks can match (CompositeRenderer::DrawEntity asks for `m_currentPass`), and a place where the
	 * target, the viewport and the material are stated explicitly instead of inherited from whatever
	 * the renderer happened to leave behind. CompositeRenderer::PresentTo drives it, because the
	 * source and the target belong to the view, not to the pass tree.
	 *
	 * Filament's blit pass is the same idea: a render-graph node whose inputs and outputs are
	 * declared, not an ad-hoc call between other work.
	 */
	class PresentPass final : public ARenderPass
	{
	public:
		explicit PresentPass(Core::CompositeRenderer& p_renderer) : ARenderPass(p_renderer) {}

	private:
		/** Driven explicitly by CompositeRenderer::PresentTo (it is a friend of this interface),
		 * which is where the source and target come from. */
		void Draw(Rendering::Data::PipelineState p_pso) override {}
	};
}
