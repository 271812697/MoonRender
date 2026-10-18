#pragma once
#include <Core/Rendering/SceneRenderer.h>
#include <Rendering/HAL/UniformBuffer.h>
#include <Rendering/Entities/Camera.h>
#include <Rendering/Core/CompositeRenderer.h>
#include <Rendering/HAL/Framebuffer.h>
#include <functional>
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
			
			/** Renders the frame into the module's present target.
			 *
			 * p_drawOverlays runs at the end, with that target bound and the module's context
			 * current: that is where the overlays (ImGui, the 2D draw list, the gizmos) are drawn,
			 * so they end up in the image the window pastes instead of being drawn on the window's
			 * own context afterwards. */
			void Render(const std::function<void()>& p_drawOverlays = {});
			void Present();
			/** The GL id of the colour texture holding the last rendered frame.
			 *
			 * This is how the frame leaves the render module: the window pastes this texture into
			 * its own framebuffer (textures are shared between the two contexts, framebuffers are
			 * not). 0 when the view has not rendered yet. */
			uint32_t GetFrameTextureID();
			virtual void DrawFrame();
			virtual ::Rendering::Entities::Camera* GetCamera() = 0;
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
			::Rendering::HAL::Framebuffer m_presentTarget;
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