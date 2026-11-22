#pragma once
#include <Core/Rendering/SceneRenderer.h>
#include <Rendering/HAL/UniformBuffer.h>
#include <Rendering/Entities/Camera.h>
#include <Rendering/Core/CompositeRenderer.h>
#include <Rendering/HAL/Framebuffer.h>
#include <cstdint>
#include <functional>
#include <vector>
#include <Rendering/View/InputState.h>
namespace Rendering::Context {
	class Driver;
}
namespace Editor {
	namespace Panels
	{
		class AView
		{
		public:

			/** p_driver is the render module's driver: the view draws through it, and it is
			 * handed in rather than looked up, so the view has no opinion about where it comes
			 * from (an editor context, a game loop, a test). */
			AView(
				const std::string& p_title,
				::Rendering::Context::Driver& p_driver
			);

			virtual void Update(float p_deltaTime);
			virtual void InitFrame();

			/** Hands one input event to the view.
			 *
			 * This is the only input entry point a surface needs: it translates whatever its window
			 * toolkit produced into a neutral InputEvent and passes it here. The view stores it in
			 * its InputState - which its camera controller reads on Update - and a subclass that
			 * needs more (a scene view picking under the cursor, say) overrides this and calls the
			 * base implementation first. */
			virtual void ReceiveEvent(const InputEvent& p_event);
			
			/** Renders the frame into the module's present target.
			 *
			 * p_drawOverlays runs at the end, with that target bound and the module's context
			 * current: that is where the overlays (ImGui, the 2D draw list, the gizmos) are drawn,
			 * so they end up in the image the window pastes instead of being drawn on the window's
			 * own context afterwards. */
			void Render(const std::function<void()>& p_drawOverlays = {});

			/** Turns the view's own frame into the image the window pastes: the scene renderer's
			 * present pass. */
			void Present();
			/** The GL id of the colour texture holding the last rendered frame.
			 *
			 * This is how the frame leaves the render module: the window pastes this texture into
			 * its own framebuffer (textures are shared between the two contexts, framebuffers are
			 * not). 0 when the view has not rendered yet. */
			uint32_t GetFrameTextureID();
			/** Blocks until the frame the window is about to paste has been drawn.
			 *
			 * That is frame N-1 (or N-2 in the demo's numbering), never the one being drawn, so
			 * this normally returns at once instead of stalling the window on the frame it just
			 * submitted (docs/RenderThreadPlan.md §8.68). */
			void WaitForPresentedFrame();
			/** Reads the last frame back into p_pixels as RGBA8, top row first.
			 *
			 * Needs the module's context current (the frame lives on it). The window layer uses
			 * this for the small images a material browser shows; it is a synchronous readback,
			 * so callers do it once per change rather than once per frame. Returns false when
			 * the view has not rendered yet. */
			bool ReadFrame(std::vector<uint8_t>& p_pixels, int& p_width, int& p_height);
			virtual void DrawFrame();
			virtual ::Rendering::Entities::Camera* GetCamera() = 0;

		/** Frames what this view is focused on, looking along p_direction.
		 *
		 * The overlay's navigation cube issues this when a face or arrow is clicked, and the overlay
		 * is engine-side: framing is a view operation, so the hook lives on the view base. The
		 * editor's view overrides it with the picking-aware version; a plain view does nothing. */
		virtual void FitToFocus(const Maths::FVector3& p_direction) { (void)p_direction; }
			virtual ::Core::SceneSystem::Scene* GetScene() = 0;
			std::pair<uint16_t, uint16_t> GetSafeSize() const;
		    ::Core::Rendering::SceneRenderer& GetRenderer() ;
			void Resize(int width, int height);
			InputState& getInutState();
			void ClearEvents();
			Maths::FVector3 GetRoaterCenter();
			void SetRotaterCenter(const Maths::FVector3&center);
			virtual ::Core::ECS::Actor* GetSelectedActor();
			virtual void SelectActor(::Core::ECS::Actor& actor);
			virtual void UnselectActor();
			virtual bool IsSelectActor();
		protected:
			virtual ::Core::Rendering::SceneRenderer::SceneDescriptor CreateSceneDescriptor();

		protected:
			Maths::FVector3 m_roaterCenter = {0,0,0};
			Maths::FVector3 m_gridColor = Maths::FVector3{ 0.176f, 0.176f, 0.176f };

			::Rendering::HAL::Framebuffer m_msaaframebuffer;
			::Rendering::HAL::Framebuffer m_framebuffer;
			/** What leaves the module: the frame after the present pass (tone mapped, RGBA8).
			 * A blit needs matching formats, and the window's framebuffer is RGBA8 while the
			 * scene buffers above are RGBA32F - so the module composes the image here and the
			 * window pastes this one. */
			/** The present ring, copied from the Qt/Filament demo's PlatformGlfwGL (RING_COUNT = 3):
			 * frame F is drawn into slot F % 3 and the window pastes slot (F - latency) % 3, so the
			 * texture being presented is never the one being drawn, and the consumer only has to
			 * wait for the frame it is about to show. One target could not do this: the paste read a
			 * texture the executor was still writing. */
			static constexpr uint32_t kPresentSlotCount = 3;
			/** How many frames behind the window presents. The demo presents two frames back
			 * (slot = (submitted - 2) % 3); one is enough with three slots and halves the visible
			 * latency. */
			static constexpr uint64_t kPresentLatencyFrames = 1;
			std::array<std::unique_ptr<::Rendering::HAL::Framebuffer>, kPresentSlotCount> m_presentTargets;
			/** Frames recorded by this view: the slot of a frame is its index % kPresentSlotCount. */
			uint64_t m_frameIndex = 0;
			/** The target the frame being recorded draws into. */
			::Rendering::HAL::Framebuffer& PresentTarget();
			/** The target holding the frame the window should paste. */
			::Rendering::HAL::Framebuffer& PresentedTarget();
			std::unique_ptr<::Core::Rendering::SceneRenderer> m_renderer;
			int mWidth = 1;
			int mHeight = 1;
			std::string name = "View";
			/** What the frame draws on top, see Render. */
			std::function<void()> m_drawOverlays;
			/** Handed in by the concrete view, see the constructor. */
			::Rendering::Context::Driver* m_driver = nullptr;
			InputState input;
		};
	}
}