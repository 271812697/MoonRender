#pragma once
#include <Core/ECS/Actor.h>
#include <Core/Resources/Material.h>
#include "GizmoBehaviour.h"
#include <Rendering/HAL/Buffer.h>
#include <cstdint>
#include <memory>
#include <vector>

namespace Editor::Rendering
{
	struct PickPassOption
	{
		bool debug = false;

	};
	class PickingRenderPass : public ::Rendering::Core::ARenderPass
	{
	public:
		using PickingResult =
			std::optional<
			std::variant<Tools::Utils::OptRef<::Core::ECS::Actor>,
			Editor::Core::GizmoBehaviour::EDirection>
			>;
		PickingRenderPass(::Rendering::Core::CompositeRenderer& p_renderer);
		PickingResult ReadbackPickingResult(
			const ::Core::SceneSystem::Scene& p_scene,
			uint32_t p_x,
			uint32_t p_y,bool& isSelected
		);

		/** Queues an asynchronous pick at (x, y).
		 *
		 * The request is served by this frame's picking pass: the pixel is copied
		 * into a pixel pack buffer and fetched one or two frames later through
		 * TryConsumePick(), so the CPU never waits for the GPU. The synchronous
		 * path above costs a full pipeline stall (measured ~10 ms per frame here)
		 * and is therefore only used for the click that must be pixel exact.
		 */
		void RequestPick(uint32_t p_x, uint32_t p_y);

		/** Non blocking fetch of a finished asynchronous pick.
		 *
		 * @return true when a readback was consumed. p_outResult is empty when
		 *         the picked pixel contained nothing (background / no gizmo).
		 */
		bool TryConsumePick(
			const ::Core::SceneSystem::Scene& p_scene,
			PickingResult& p_outResult,
			uint32_t& p_outX,
			uint32_t& p_outY
		);

		PickPassOption& GetPickPassOption();
	private:
		PickingResult DecodePickResult(
			const ::Core::SceneSystem::Scene& p_scene,
			const uint8_t p_pixel[4],
			bool& p_isSelected
		);
		/** Conservative screen-space rejection for the picking pass.
		 *
		 * The pass is draw-call bound on big scenes (one draw per mesh), and only
		 * the pixels inside the pick region are read back, so drawables whose
		 * bounding sphere cannot touch that region are skipped entirely. The test
		 * uses the drawable's own bounds, so it stays correct even when the scene
		 * BVH is stale.
		 */
		bool MayTouchPickRegion(const ::Rendering::Entities::Drawable& p_drawable) const;
		void SetupPickReadbacks();
		void IssuePickReadback();

		virtual void Draw(::Rendering::Data::PipelineState p_pso) override;
		void DrawPickableModels(::Rendering::Data::PipelineState p_pso, ::Core::SceneSystem::Scene& p_scene);
	private:
		::Rendering::HAL::Framebuffer m_actorPickingFramebuffer;
		::Core::Resources::Material m_actorPickingFallbackMaterial;
		::Core::Resources::Material m_TopoShapePickingFallbackMaterial;
		PickPassOption mPickOption;

		/** One in flight asynchronous picking readback (RGBA8, 1 pixel). */
		struct PickReadbackSlot
		{
			std::shared_ptr<::Rendering::HAL::Buffer> buffer;
			uint32_t x = 0;
			uint32_t y = 0;
			bool pending = false;
		};
		static constexpr uint32_t kPickReadbackSlotCount = 3;
		static constexpr uint64_t kPickReadbackBytes = 4;

		std::vector<PickReadbackSlot> m_pickReadbackSlots;
		uint32_t m_pickWriteIndex = 0;
		bool m_pickRequestPending = false;
		uint32_t m_pickRequestX = 0;
		uint32_t m_pickRequestY = 0;

		/** Pick region / matrices captured by Draw() for MayTouchPickRegion(). */
		float m_pickRegionNdcMinX = -1.0f;
		float m_pickRegionNdcMinY = -1.0f;
		float m_pickRegionNdcMaxX = 1.0f;
		float m_pickRegionNdcMaxY = 1.0f;
		float m_pickProjectionScaleX = 1.0f;
		float m_pickProjectionScaleY = 1.0f;
		Maths::FMatrix4 m_pickViewProjection = Maths::FMatrix4::Identity;
	};
}
