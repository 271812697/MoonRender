#include <tracy/Tracy.hpp>
#include <algorithm>
#include <Core/Rendering/FramebufferUtil.h>
#include <Core/Rendering/HzbBuildPass.h>
#include <memory>
#include <vector>
#include <Rendering/View/AView.h>
#include <Rendering/HAL/Profiling.h>
#include "Rendering/HAL/Common/TFramebuffer.h"
#include "Rendering/HAL/Common/TTexture.h"
#include <Rendering/Context/IGLContextHost.h>
#include <Rendering/Context/Driver.h>
#include <Rendering/Utils/FrameTrace.h>
#include <cstdio>
#include <cstdlib>
#include <iostream>
namespace
{
	/** MOON_FRAME_TRACE prints the frame's milestones to stderr: the bring-up tool for finding out
	 * which step blocks when the render thread owns the context. */
	void TraceView(const char* p_what)
	{
		static const bool enabled = std::getenv("MOON_FRAME_TRACE") != nullptr;
		if (!enabled)
		{
			return;
		}
		// Binds and draws recorded between two passes are worth attributing too, so the milestone
		// doubles as the executor's label (Rendering/Utils/FrameTrace.h).
		::Rendering::Utils::FrameTrace::SetPass(p_what);
		std::fprintf(stderr, "[view] %s\n", p_what);
		std::fflush(stderr);
	}
}
Editor::Panels::AView::AView
(
	const std::string& p_title,
	::Rendering::Context::Driver& p_driver) :
	m_driver(&p_driver), m_framebuffer(p_title), m_presentTarget(p_title + "/Present"), name(p_title)
{
	m_driver->SetupFramebuffer(m_framebuffer, 1u, 1u, true, true, false, false);
	m_driver->SetupFramebuffer(m_msaaframebuffer, 1u, 1u, true, true, false, true);
	// RGBA8 on purpose: it is what the window's framebuffer is, and a blit between the two
	// needs the same format (the scene buffers above are RGBA32F). Mutable storage, because the
	// target is resized as the window resizes.
	const ::Rendering::Settings::TextureDesc presentDesc{
		.width = 1,
		.height = 1,
		.minFilter = ::Rendering::Settings::ETextureFilteringMode::LINEAR,
		.magFilter = ::Rendering::Settings::ETextureFilteringMode::LINEAR,
		.horizontalWrap = ::Rendering::Settings::ETextureWrapMode::CLAMP_TO_EDGE,
		.verticalWrap = ::Rendering::Settings::ETextureWrapMode::CLAMP_TO_EDGE,
		.internalFormat = ::Rendering::Settings::EInternalFormat::RGBA8,
		.useMipMaps = false,
		.mutableDesc = ::Rendering::Settings::MutableTextureDesc{
			.format = ::Rendering::Settings::EFormat::RGBA,
			.type = ::Rendering::Settings::EPixelDataType::UNSIGNED_BYTE
		}
	};
	m_driver->SetupFramebuffer(m_presentTarget, presentDesc, false, false, false);

	// The three setups above are recorded. Everything that reads the frame - the widget that pastes
	// it, the overlay - asks this view for the texture, so what was recorded has to run before the
	// constructor returns, or a target would still have no attachments when someone looks.
	m_driver->ExecutePendingCommands();
}
void Editor::Panels::AView::ReceiveEvent(const InputEvent& p_event)
{
	input.ReceiveEvent(p_event);
}
void Editor::Panels::AView::Update(float p_deltaTime)
{
}
void Editor::Panels::AView::InitFrame()
{
	m_renderer->AddDescriptor<::Core::Rendering::SceneRenderer::SceneDescriptor>(
		CreateSceneDescriptor()
	);
}
namespace
{
	/** Binds the render module's context for the duration of a frame and releases it afterwards
	 * (leaving no context current - the window layer binds its own again before it touches GL).
	 *
	 * A caller that already bound the context for the whole frame (so that the overlays it draws
	 * after the frame render on the same context) is left alone: the scope only binds and
	 * releases what it bound itself.
	 *
	 * Does nothing when the driver has no context host: a host that does not provide one keeps
	 * the old behaviour, rendering on whatever context happens to be current, which is what
	 * every caller did before the module had a context of its own. */
	struct ModuleContextScope
	{
		explicit ModuleContextScope(::Rendering::Context::Driver* p_driver)
		{
			// With a render thread the context belongs to it and must never be made current here:
			// recording needs no context, and Qt aborts the process if another thread tries.
			if (p_driver != nullptr && p_driver->IsRenderThreadActive())
			{
				return;
			}
			if (p_driver != nullptr && p_driver->GetContextHost() != nullptr)
			{
				m_host = p_driver->GetContextHost();
				// Already bound by the caller (the window layer binds it for the whole frame, so
				// that the overlays it draws afterwards land on the same context): leave it alone,
				// and do not release it on the way out either.
				if (m_host->IsCurrent())
				{
					m_alreadyCurrent = true;
				}
				else
				{
					m_bound = m_host->MakeCurrent();
				}
			}
		}
		~ModuleContextScope()
		{
			if (m_bound)
			{
				m_host->DoneCurrent();
			}
		}
		/** False when the module has a context but it could not be made current: the caller
		 * must not render, or its GL calls would land on the wrong context. */
		bool CanRender() const
		{
			return m_host == nullptr || m_bound || m_alreadyCurrent;
		}
	private:
		::Rendering::Context::IGLContextHost* m_host = nullptr;
		bool m_bound = false;
		bool m_alreadyCurrent = false;
	};
}
void Editor::Panels::AView::Render(const std::function<void()>& p_drawOverlays)
{
	m_drawOverlays = p_drawOverlays;
	auto* driver = m_driver;
	ModuleContextScope moduleContext(driver);
	if (!moduleContext.CanRender())
	{
		return;
	}
	auto [winWidth, winHeight] = GetSafeSize();
	auto camera = GetCamera();
	auto scene = GetScene();
	if (winWidth > 1 && winHeight > 1 && camera && scene)
	// Minimal path for the render-thread bring-up (MOON_RENDER_MINIMAL): the frame's content is
	// recorded and run on the side that owns the context - the resizes, the pass tree and the
	// present. The overlays stay out; they are the next layer.
	if (std::getenv("MOON_RENDER_MINIMAL") != nullptr && driver->IsRenderThreadActive())
	{
		FrameMarkStart(name.c_str());
		InitFrame();
		Rendering::Data::FrameDescriptor frameDescriptor;
		frameDescriptor.renderWidth = winWidth;
		frameDescriptor.renderHeight = winHeight;
		frameDescriptor.camera = camera;
		frameDescriptor.outputMsaaBuffer = m_msaaframebuffer;
		frameDescriptor.presentBuffer = m_framebuffer;

		// The frame's first command, as in Filament (FRenderer::beginFrame -> FSwapChain::makeCurrent
		// -> driver.makeCurrent): the executor makes the module's context current before it runs
		// anything else in this batch.
		m_driver->MakeCurrent();

		m_driver->ResizeFramebuffer(m_framebuffer, winWidth, winHeight);
		m_driver->ResizeFramebuffer(m_msaaframebuffer, winWidth, winHeight);
		m_driver->ResizeFramebuffer(m_presentTarget, winWidth, winHeight);
		// HZB issues its own GL - fences and an asynchronous readback through a PBO ring - so it
		// cannot run on the recording side yet. With a render thread it stays off until that
		// readback becomes a command (docs/RenderThreadPlan.md).
		if (m_driver->IsRenderThreadActive())
		{
			m_renderer->GetPass<::Core::Rendering::HzbBuildPass>("HZB").SetEnabled(false);
		}
		TraceView("BeginFrame");
		m_renderer->BeginFrame(frameDescriptor);
		TraceView("DrawFrame");
		DrawFrame();
		TraceView("EndFrame");
		m_renderer->EndFrame();
		TraceView("CopyFramebufferColor");
		m_driver->CopyFramebufferColor(m_msaaframebuffer, 0, m_framebuffer, 0);
		TraceView("PresentTo");
		m_renderer->PresentTo(m_framebuffer, m_presentTarget);
		TraceView("flush");
		// Recorded, not executed: the batch is published here and run by the consumer's
		// Driver::Execute() (see ViewportFrame::Render) - Filament's render() only flushes.
		m_driver->Flush();
		TraceView("BindFramebuffer(nullptr)");
		m_driver->BindFramebuffer(nullptr);
		TraceView("done");
		FrameMarkEnd(name.c_str());
		return;
	}
	{
		FrameMarkStart(name.c_str());
		InitFrame();
		Rendering::Data::FrameDescriptor frameDescriptor;
		frameDescriptor.renderWidth = winWidth;
		frameDescriptor.renderHeight = winHeight;
		frameDescriptor.camera = camera;
		frameDescriptor.outputMsaaBuffer = m_msaaframebuffer;
		frameDescriptor.presentBuffer = m_framebuffer;
		// The scene's pass tree is GL that the driver does not describe one call at a time: the
		// whole tree - the render-target resizes, frame setup, every pass, the MSAA resolve - is
		// recorded as one block and runs when the frame's batch is executed, on the side that owns
		// Recorded, not executed here: each of these becomes a command, and the batch they belong
		// to is run on the side that owns the context. Nothing in this frame runs "inside" a
		// command any more, so a command can never record another one.
		// The frame's first command, as in Filament (FRenderer::beginFrame -> FSwapChain::makeCurrent
		// -> driver.makeCurrent): the executor makes the module's context current before it runs
		// anything else in this batch.
		m_driver->MakeCurrent();

		// The frame's first command, as in Filament (FRenderer::beginFrame -> FSwapChain::makeCurrent
		// -> driver.makeCurrent): the executor makes the module's context current before it runs
		// anything else in this batch.
		m_driver->MakeCurrent();

		m_driver->ResizeFramebuffer(m_framebuffer, winWidth, winHeight);
		m_driver->ResizeFramebuffer(m_msaaframebuffer, winWidth, winHeight);
		m_driver->ResizeFramebuffer(m_presentTarget, winWidth, winHeight);
		// HZB issues its own GL - fences and an asynchronous readback through a PBO ring - so it
		// cannot run on the recording side yet. With a render thread it stays off until that
		// readback becomes a command (docs/RenderThreadPlan.md).
		if (m_driver->IsRenderThreadActive())
		{
			m_renderer->GetPass<::Core::Rendering::HzbBuildPass>("HZB").SetEnabled(false);
		}
		TraceView("BeginFrame");
		m_renderer->BeginFrame(frameDescriptor);

		// MOON_FRAME_TRACE, threaded: the centre pixel of a target, read on the executor. Three of
		// these in a row say where a frame loses its content - after the scene, after the resolve,
		// after the present.
		const bool probePixels = m_driver->IsRenderThreadActive()
			&& std::getenv("MOON_FRAME_TRACE") != nullptr;
		auto probe = [this, probePixels, winWidth, winHeight]
			(const char* p_label, ::Rendering::HAL::Framebuffer& p_target)
		{
			if (!probePixels)
			{
				return;
			}
			auto pixels = std::make_shared<std::vector<uint8_t>>(4);
			::Rendering::Backend::ReadPixelsRequest request;
			request.framebufferId = p_target.GetID();
			request.readBufferIndex = 0;
			request.x = static_cast<uint32_t>(winWidth) / 2;
			request.y = static_cast<uint32_t>(winHeight) / 2;
			request.width = 1;
			request.height = 1;
			request.format = ::Rendering::Settings::EPixelDataFormat::RGBA;
			request.type = ::Rendering::Settings::EPixelDataType::UNSIGNED_BYTE;
			request.data = pixels;
			const std::string label(p_label);
			request.onDone = [pixels, label] {
				std::fprintf(stderr, "[probe] %s = %u %u %u %u\n", label.c_str(),
					(*pixels)[0], (*pixels)[1], (*pixels)[2], (*pixels)[3]);
				std::fflush(stderr);
			};
			m_driver->ReadPixels(request);
			m_driver->ExecutePendingCommands();
		};

		TraceView("DrawFrame");
		DrawFrame();
		probe("after scene (msaa)", m_msaaframebuffer);
		TraceView("EndFrame");
		m_renderer->EndFrame();
		TraceView("CopyFramebufferColor");
		m_driver->CopyFramebufferColor(m_msaaframebuffer, 0, m_framebuffer, 0);
		probe("after resolve", m_framebuffer);
		// Compose what leaves the module: the present is a pass of its own - PresentTo states its
		// source, its target and its viewport - but it runs here rather than inside the batch
		// above. Recording it loses the navigation cube's six arrows and the rotation icons, and
		// that happens whether the present is a pass or a plain call, so the cause is on the
		// overlay side (docs/RenderThreadPlan.md §7.12).
		TraceView("PresentTo");
		m_renderer->PresentTo(m_framebuffer, m_presentTarget);
		// The overlays are part of the frame the window pastes: ImGui, the 2D draw list and the
		// gizmos. Drawn here, into the same target, on the same context - which is what keeps
		// them out of the window layer, and the module free of it.
		if (m_drawOverlays)
		{
			m_drawOverlays();
		}
		// End of the recording: the batch is published, not executed. Execution belongs to the
		// frame's consumer (Driver::Execute, once per frame, outside the render call) - Filament's
		// Engine::execute in the app loop, or the driver thread's loop when there is one. Keeping
		// the two apart is what makes this call record-only: nothing here needs the GL result.
		m_driver->Flush();
		m_driver->BindFramebuffer(nullptr);
		FrameMarkEnd(name.c_str());
		m_driver->OnFrameCompleted();

		// MOON_FRAME_TRACE, threaded: is the present target actually holding a frame? The paste on
		// the window side blits this texture, so an empty one here is what a blank viewport means.
		// The read runs on the executor (ReadPixels is a command) and prints from there.
		if (m_driver->IsRenderThreadActive() && std::getenv("MOON_FRAME_TRACE") != nullptr)
		{
			auto pixels = std::make_shared<std::vector<uint8_t>>(4);
			::Rendering::Backend::ReadPixelsRequest probe;
			probe.framebufferId = m_presentTarget.GetID();
			probe.readBufferIndex = 0;
			probe.x = static_cast<uint32_t>(winWidth) / 2;
			probe.y = static_cast<uint32_t>(winHeight) / 2;
			probe.width = 1;
			probe.height = 1;
			probe.format = ::Rendering::Settings::EPixelDataFormat::RGBA;
			probe.type = ::Rendering::Settings::EPixelDataType::UNSIGNED_BYTE;
			probe.data = pixels;
			probe.onDone = [pixels] {
				std::fprintf(stderr, "[present] centre pixel = %u %u %u %u\n",
					(*pixels)[0], (*pixels)[1], (*pixels)[2], (*pixels)[3]);
				std::fflush(stderr);
			};
			m_driver->ReadPixels(probe);
			m_driver->ExecutePendingCommands();
		}
	}
}
void Editor::Panels::AView::Present()
{
	m_renderer->Present(m_framebuffer);
}
uint32_t Editor::Panels::AView::GetFrameTextureID()
{
	auto color = m_presentTarget.GetAttachment<::Rendering::HAL::Texture>(
		::Rendering::Settings::EFramebufferAttachment::COLOR);
	return color.has_value() ? static_cast<uint32_t>(color.value().GetID()) : 0u;
}
bool Editor::Panels::AView::ReadFrame(std::vector<uint8_t>& p_pixels, int& p_width, int& p_height)
{
	if (m_presentTarget.GetID() == 0)
	{
		return false;
	}
	const auto [width, height] = m_presentTarget.GetSize();
	if (width == 0 || height == 0)
	{
		return false;
	}
	p_width = static_cast<int>(width);
	p_height = static_cast<int>(height);
	p_pixels.resize(static_cast<size_t>(p_width) * p_height * 4);
	// ReadPixels binds the target for the read and unbinds it again.
	m_presentTarget.ReadPixels(0, 0, width, height,
		::Rendering::Settings::EPixelDataFormat::RGBA,
		::Rendering::Settings::EPixelDataType::UNSIGNED_BYTE,
		p_pixels.data());
	// glReadPixels hands back the bottom row first; images go the other way.
	for (int y = 0; y < p_height / 2; ++y)
	{
		auto* top = p_pixels.data() + static_cast<size_t>(y) * p_width * 4;
		auto* bottom = p_pixels.data() + static_cast<size_t>(p_height - 1 - y) * p_width * 4;
		std::swap_ranges(top, top + static_cast<size_t>(p_width) * 4, bottom);
	}
	return true;
}
void Editor::Panels::AView::DrawFrame()
{
	m_renderer->DrawFrame();
}
std::pair<uint16_t, uint16_t> Editor::Panels::AView::GetSafeSize() const
{
	return { mWidth,mHeight };
}
 Core::Rendering::SceneRenderer& Editor::Panels::AView::GetRenderer() 
{
	return *m_renderer.get();
}
Core::Rendering::SceneRenderer::SceneDescriptor Editor::Panels::AView::CreateSceneDescriptor()
{
	auto scene = GetScene();
	assert(scene&&"No scene assigned to this view!");
	return {
		*scene
	};
}
void Editor::Panels::AView::Resize(int width, int height)
{
	mWidth = width;
	mHeight = height;
	m_renderer->Resize(width, height);
}
namespace Editor::Panels {
	InputState& Editor::Panels::AView::getInutState()
	{
		return input;
	}
	void AView::ClearEvents()
	{
		input.ClearEvents();
	}
	Maths::FVector3 AView::GetRoaterCenter()
	{
		return m_roaterCenter;
	}
	void AView::SetRotaterCenter(const Maths::FVector3& center)
	{
		m_roaterCenter = center;
	}
	::Core::ECS::Actor* AView::GetSelectedActor()
	{
		return nullptr;
	}
	void AView::SelectActor(::Core::ECS::Actor& actor)
	{
	}
	void AView::UnselectActor()
	{
	}
	bool AView::IsSelectActor()
	{
		return false;
	}
}
