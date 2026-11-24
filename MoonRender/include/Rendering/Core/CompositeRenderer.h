#pragma once
#include <Rendering/Data/PipelineState.h>
#include <typeindex>
#include <memory>
#include <Rendering/Core/ABaseRenderer.h>
#include <Rendering/Core/ARenderPass.h>
#include <Rendering/Core/PresentPass.h>
#include <Rendering/Data/Describable.h>
#include <Rendering/Features/ARenderFeature.h>
#include <Rendering/Types/RenderFeatureType.h>
#include <Rendering/Types/RenderPassType.h>

namespace Rendering::Core
{
	class CompositeRenderer : public ABaseRenderer, public Data::Describable
	{
	public:
		Tools::Eventing::Event<Rendering::Data::PipelineState&, const Entities::Drawable&> preDrawEntityEvent;
		Tools::Eventing::Event<const Entities::Drawable&> postDrawEntityEvent;

		CompositeRenderer(Context::Driver& p_driver);

		virtual void BeginFrame(const Data::FrameDescriptor& p_frameDescriptor);

		virtual void DrawFrame() final;

		/** The present, as the last thing the frame draws: reads p_source's colour attachment and
		 * writes p_target, with the pass context set explicitly (see PresentPass). */
		void PresentTo(::Rendering::Data::Framebuffer& p_source, ::Rendering::Data::Framebuffer& p_target);

		virtual void EndFrame() override;


		virtual void DrawEntity(
			Rendering::Data::PipelineState p_pso,
			const Entities::Drawable& p_drawable
		) override;


		template<Types::RenderFeatureType T, Features::EFeatureExecutionPolicy Policy, typename ... Args>
		T& AddFeature(Args&&... p_args);

		template<Types::RenderFeatureType T>
		bool RemoveFeature();

		template<Types::RenderFeatureType T>
		T& GetFeature() const;

		template<Types::RenderFeatureType T>
		bool HasFeature() const;

		template<Types::RenderPassType T, typename ... Args>
		T& AddPass(const std::string& p_name, uint32_t p_order, Args&&... p_args);

		template<Types::RenderPassType T>
		T& GetPass(const std::string& p_name) const;
		std::multimap<uint32_t, std::pair<std::string, std::unique_ptr<Core::ARenderPass>>>& GetPasses() {
			return m_passes;
		}
		::Rendering::HAL::Texture* GetSkyBoxCube();
		::Rendering::HAL::Texture* GetIrradianceCube();
		::Rendering::HAL::Texture* GetPrefilterCube();
		::Rendering::HAL::Texture* GetBrdfTexture();
	protected:
		std::unordered_map<std::type_index, std::unique_ptr<Features::ARenderFeature>> m_features;
		std::multimap<uint32_t, std::pair<std::string, std::unique_ptr<Core::ARenderPass>>> m_passes;

	private:
		Tools::Utils::OptRef<Core::ARenderPass> m_currentPass;
		/** The present's pass object: not in m_passes, because its target belongs to the view. */
		std::unique_ptr<PresentPass> m_presentPass;
	};
}

#include "Rendering/Core/CompositeRenderer.inl"
