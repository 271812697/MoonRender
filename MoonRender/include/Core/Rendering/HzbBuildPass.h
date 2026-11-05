#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <Core/Rendering/HzbCuller.h>
#include <Core/Resources/MaterialAsset.h>
#include <Rendering/Core/ARenderPass.h>
#include <Rendering/HAL/Buffer.h>
#include <Rendering/HAL/Framebuffer.h>

namespace Core::Rendering
{
	/** Builds the hierarchical Z-buffer (max depth pyramid) of the current frame.
	 *
	 * Source depth is resolved from the frame's MSAA depth buffer, so the
	 * occluder set is exactly the opaque (plus section cap) geometry: the depth
	 * peeling pass keeps transparents in its own layer buffers. The pyramid is
	 * reduced down to a small grid that is read back to the CPU for the next
	 * frame's BVH occlusion test (one frame of latency, no GPU stall besides the
	 * small readback).
	 */
	class HzbBuildPass : public ::Rendering::Core::ARenderPass
	{
	public:
		HzbBuildPass(::Rendering::Core::CompositeRenderer& p_renderer);
		~HzbBuildPass();

		void SetCuller(HzbCuller* p_culler) { m_culler = p_culler; }
		void SetMaxGridSize(uint32_t p_size) { m_maxGridSize = p_size; }
		uint32_t GetMaxGridSize() const { return m_maxGridSize; }

		/** Relative depth bias for the CPU culler (exposed in pass settings). */
		void SetDepthBias(float p_bias) { m_depthBias = p_bias; }
		float GetDepthBias() const { return m_depthBias; }

		/** Bias used while the camera is static (previous depth is still exact). */
		void SetStaticDepthBias(float p_bias) { m_staticDepthBias = p_bias; }
		float GetStaticDepthBias() const { return m_staticDepthBias; }

		uint32_t GetGridWidth() const { return m_gridWidth; }
		uint32_t GetGridHeight() const { return m_gridHeight; }
		float GetLastBuildTimeMs() const { return m_lastBuildTimeMs; }

		/** Asynchronous readback state (debug overlay / profiling). */
		uint32_t GetReadbackSlotCount() const
		{
			return static_cast<uint32_t>(m_readbackSlots.size());
		}
		uint32_t GetReadbackPendingSlots() const { return m_readbackPendingSlots; }
		uint32_t GetReadbackSkippedFrames() const { return m_readbackSkippedFrames; }
		uint32_t GetLastReadbackLatencyFrames() const { return m_lastReadbackLatencyFrames; }

	protected:
		virtual void Draw(::Rendering::Data::PipelineState p_pso) override;
		virtual void ResizeRenderer(int width, int height) override;

	private:
		void SetupTargets(uint32_t p_width, uint32_t p_height);
		void ReleaseLevels();

		/** (Re)allocates the pixel pack buffer ring for the current grid size. */
		void SetupReadbacks();
		void ReleaseReadbacks();
		/** Feeds the culler with the oldest finished readback slot, if any. */
		void ConsumeReadyReadback();
		/** Submits this frame's readback; skipped when the ring is saturated. */
		void IssueReadback();

		::Core::Resources::MaterialAsset m_reduceMaterial;

		/** Depth only framebuffer holding the resolved (non MSAA) scene depth. */
		::Rendering::HAL::Framebuffer m_resolveFbo;
		std::shared_ptr<::Rendering::HAL::Texture> m_resolveColor;
		std::shared_ptr<::Rendering::HAL::Texture> m_resolveDepth;

		/** One framebuffer per pyramid level (R32F color, half resolution each step).
		 *
		 * Held by unique_ptr on purpose: GLFramebuffer has no user defined
		 * copy/move and its destructor calls glDeleteFramebuffers, so storing the
		 * framebuffers by value in a growing vector would let the temporary
		 * copies destroy the GL names still referenced by the surviving objects
		 * (surfaces as "Framebuffer name must be generated before being bound").
		 */
		std::vector<std::unique_ptr<::Rendering::HAL::Framebuffer>> m_levels;
		std::vector<Maths::FVector2> m_levelResolutions;
		std::vector<std::shared_ptr<::Rendering::HAL::Texture>> m_levelTextures;

		/** One entry of the asynchronous readback ring.
		 *
		 * The grid is copied into a PBO instead of being read synchronously, and
		 * fetched a few frames later once its fence is signalled, so the CPU
		 * never waits for the GPU to finish the frame.
		 */
		struct ReadbackSlot
		{
			std::shared_ptr<::Rendering::HAL::Buffer> buffer;
			bool pending = false;
			uint64_t issueFrame = 0;
		};

		static constexpr uint32_t kReadbackSlotCount = 3;
		std::vector<ReadbackSlot> m_readbackSlots;
		uint64_t m_readbackBytes = 0;
		uint32_t m_readbackWriteIndex = 0;
		uint32_t m_readbackPendingSlots = 0;
		uint32_t m_readbackSkippedFrames = 0;
		uint32_t m_lastReadbackLatencyFrames = 0;
		uint64_t m_frameCounter = 0;

		HzbCuller* m_culler = nullptr;
		uint32_t m_maxGridSize = 64;
		float m_depthBias = 0.0005f;
		float m_staticDepthBias = 1e-6f;
		uint32_t m_gridWidth = 0;
		uint32_t m_gridHeight = 0;
		uint32_t m_width = 0;
		uint32_t m_height = 0;
		float m_lastBuildTimeMs = 0.0f;
		bool m_targetsReady = false;
	};
}
