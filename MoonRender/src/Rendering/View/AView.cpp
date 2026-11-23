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
	m_driver(&p_driver), m_framebuffer(p_title), name(p_title)
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
	// The present ring (see kPresentSlotCount): each slot is a complete frame target of its own.
	for (uint32_t slot = 0; slot < kPresentSlotCount; ++slot)
	{
		m_presentTargets[slot] = std::make_unique<::Rendering::HAL::Framebuffer>(
			p_title + "/Present" + std::to_string(slot));
		m_driver->SetupFramebuffer(*m_presentTargets[slot], presentDesc, false, false, false);
	}

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
	//if (std::getenv("MOON_RENDER_MINIMAL") != nullptr && driver->IsRenderThreadActive())
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
		// Every slot, not only the one this frame draws into: the slot the window is presenting kept
		// the old size after a resize, and blitting a stale-sized texture into the widget scaled it
		// and left the rest of the widget holding the previous frame - the "ghost" (docs 8.68).
		for (const auto& presentSlot : m_presentTargets)
		{
			m_driver->ResizeFramebuffer(*presentSlot, winWidth, winHeight);
		}
		// HZB issues its own GL - fences and an asynchronous readback through a PBO ring - so it
		// cannot run on the recording side yet. It stays off in both configurations, because the
		// frame is recorded the same way in both; it comes back when that readback becomes a
		// command (docs/RenderThreadPlan.md §8.72).
		m_renderer->GetPass<::Core::Rendering::HzbBuildPass>("HZB").SetEnabled(false);
		TraceView("BeginFrame");
		m_renderer->BeginFrame(frameDescriptor);
		TraceView("DrawFrame");
		DrawFrame();
		TraceView("EndFrame");
		m_renderer->EndFrame();
		TraceView("CopyFramebufferColor");
		m_driver->CopyFramebufferColor(m_msaaframebuffer, 0, m_framebuffer, 0);
		TraceView("PresentTo");
		const uint32_t slot = mDriverSlot %3;
		m_renderer->PresentTo(m_framebuffer, *m_presentTargets[static_cast<size_t>(slot)]);
		
		auto commitView = [this]() {
			this->Commit();
		};
		m_driver->Enqueue(commitView);
		TraceView("flush");
		m_driver->Flush();
		++mSubmittedFrames;
		if (mSubmittedFrames >= 2) {
			WaitForPresentedFrame(mSubmittedFrames-1);
		}
		TraceView("BindFramebuffer(nullptr)");
		m_driver->BindFramebuffer(nullptr);
		TraceView("done");
		FrameMarkEnd(name.c_str());
		return;
	}
}
void Editor::Panels::AView::Present()
{
	m_renderer->Present(m_framebuffer);
}
void Editor::Panels::AView::Commit() {
	std::lock_guard<std::mutex> lock(mMutex);
	mDriverSlot++;
	mCond.notify_all();
}

uint32_t Editor::Panels::AView::GetFrameTextureID()
{
	auto color = PresentedTarget().GetAttachment<::Rendering::HAL::Texture>(
		::Rendering::Settings::EFramebufferAttachment::COLOR);
	return color.has_value() ? static_cast<uint32_t>(color.value().GetID()) : 0u;
}

void Editor::Panels::AView::WaitForPresentedFrame(uint64_t frames)
{
	std::unique_lock<std::mutex> lock(mMutex);
	mCond.wait(lock, [&]() { return mDriverSlot >= frames; });
}



::Rendering::HAL::Framebuffer& Editor::Panels::AView::PresentedTarget()
{
	const int slot = (int)((mSubmittedFrames - 2) % 3);
	return *m_presentTargets[static_cast<size_t>(slot)];
}
bool Editor::Panels::AView::ReadFrame(std::vector<uint8_t>& p_pixels, int& p_width, int& p_height)
{
	if (PresentedTarget().GetID() == 0)
	{
		return false;
	}
	const auto [width, height] = PresentedTarget().GetSize();
	if (width == 0 || height == 0)
	{
		return false;
	}
	p_width = static_cast<int>(width);
	p_height = static_cast<int>(height);
	p_pixels.resize(static_cast<size_t>(p_width) * p_height * 4);
	// ReadPixels binds the target for the read and unbinds it again.
	PresentedTarget().ReadPixels(0, 0, width, height,
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
	// Every slot is its own target, so every one of them has to follow the window.
	for (const auto& slot : m_presentTargets)
	{
		if (slot != nullptr)
		{
			m_driver->ResizeFramebuffer(*slot, static_cast<uint16_t>(width),
				static_cast<uint16_t>(height));
		}
	}
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
