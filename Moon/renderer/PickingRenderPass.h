#pragma once
#include <Rendering/Data/BufferObject.h>
#include <Rendering/Data/PipelineState.h>
#include <atomic>
#include <Core/ECS/Actor.h>
#include <Core/Resources/MaterialAsset.h>
#include "GizmoBehaviour.h"
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

		/** One pick: what was under the pixel, and where on the geometry that pixel was.
		 *
		 * The world position is not a second query: the picking target has a second color
		 * attachment every geometry pick shader writes the fragment's world position into,
		 * so the pixel's id and its position are read back together. This is what the
		 * camera turns around when the user right-clicks - no ray through the scene, and
		 * nothing that can disagree with what is on screen. */
		struct PickHit
		{
			PickingResult result;
			Maths::FVector3 worldPosition = Maths::FVector3::Zero;
			bool hasWorldPosition = false;
			/** Whether the pixel belonged to something pickable at all. A click that lands on
			 * nothing clears the selection, and that decision is this flag's. */
			bool hitSomething = false;
		};

		PickingRenderPass(::Rendering::Core::CompositeRenderer& p_renderer);

		/** Queues an asynchronous pick at (x, y).
		 *
		 * The request is served by this frame's picking pass: the pixel is copied
		 * into a pixel pack buffer and fetched one or two frames later through
		 * TryConsumePick(), so the CPU never waits for the GPU. Clicks use it too -
		 * a synchronous readback cost a full pipeline stall (~10 ms) twice per
		 * click, and the pixel it returned was the same one.
		 */
		void RequestPick(uint32_t p_x, uint32_t p_y);

		/** Non blocking fetch of a finished asynchronous pick.
		 *
		 * @return true when a readback was consumed. p_outResult is empty when
		 *         the picked pixel contained nothing (background / no gizmo).
		 */
		bool TryConsumePick(
			const ::Core::SceneSystem::Scene& p_scene,
			PickHit& p_outHit,
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
		::Rendering::Data::Framebuffer m_actorPickingFramebuffer;
		::Core::Resources::MaterialAsset m_actorPickingFallbackMaterial;
		::Core::Resources::MaterialAsset m_TopoShapePickingFallbackMaterial;
		PickPassOption mPickOption;

		/** One in flight picking readback: the id pixel and the world-position pixel of the same
		 * spot, read by two read-pixels commands on the side that owns the context. */
		/** One in-flight pick: the pixel-pack buffer both reads land in, and the fence that says
		 * when the GPU finished writing it. glReadPixels into a buffer is the asynchronous form -
		 * the driver DMAs and the CPU is not involved - so the render side does not stall either;
		 * the fence is polled by the fetch, and the bytes are copied only once it is signalled
		 * (Filament's readPixels into a PBO + getFenceStatus, folded into DownloadBufferObject). */
		struct PickReadbackSlot
		{
			PickReadbackSlot() : buffer(::Rendering::Settings::EBufferType::PIXEL_PACK) {}

			::Rendering::Data::BufferObject buffer;
			/** Where the fence-gated fetch puts the bytes, and the flag it flips when it did:
			 * 0 = in flight, 1 = copied, 2 = fence not signalled yet. */
			std::shared_ptr<std::vector<uint8_t>> cpuBytes;
			std::shared_ptr<std::atomic<int>> downloadState;
			bool downloadIssued = false;
			uint32_t x = 0;
			uint32_t y = 0;
			bool pending = false;
		};
		static constexpr uint32_t kPickReadbackSlotCount = 3;
		/** One pixel of the id attachment (RGBA8) at offset 0, one of the world position
		 * attachment (RGBA32F) behind it. */
		static constexpr uint64_t kPickReadbackIdBytes = 4;
		static constexpr uint64_t kPickReadbackPositionOffset = 16;
		static constexpr uint64_t kPickReadbackBytes = 32;

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
