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
	
	// Built on this side, by recorded commands (see FramebufferUtil::SetupFramebufferWithDriver):
	// the attachments are front-end textures with handles, so the view can hand its frame texture to
	// the window at any time. A target whose attachments the executor created would report an empty
	// attachment list here until the command ran.
	::Core::Rendering::FramebufferUtil::SetupFramebuffer(m_framebuffer, 1u, 1u, true, true, false, false);
	::Core::Rendering::FramebufferUtil::SetupFramebuffer(m_msaaframebuffer, 1u, 1u, true, true, false, true);
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
		m_presentTargets[slot] = std::make_unique<::Rendering::Data::Framebuffer>(
			p_title + "/Present" + std::to_string(slot));
		::Core::Rendering::FramebufferUtil::SetupFramebuffer(
			*m_presentTargets[slot], presentDesc, false, false, false);
	}

}void Editor::Panels::AView::ReceiveEvent(const InputEvent& p_event)
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
}
void Editor::Panels::AView::Render(const std::function<void()>& p_drawOverlays)
{
	m_drawOverlays = p_drawOverlays;
	// Recording needs no context: everything below becomes commands, and the frame's own first
	// command activates the module's context on the executor (Driver::MakeCurrent). Nothing here
	// borrows the context from the executor, in either configuration.
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

		m_driver->ResizeFramebuffer(m_framebuffer, winWidth, winHeight);
		m_driver->ResizeFramebuffer(m_msaaframebuffer, winWidth, winHeight);
		// Every slot, not only the one this frame draws into: the slot the window is presenting kept
		// the old size after a resize, and blitting a stale-sized texture into the widget scaled it
		// and left the rest of the widget holding the previous frame - the "ghost" (docs 8.68).
		for (const auto& presentSlot : m_presentTargets)
		{
			m_driver->ResizeFramebuffer(*presentSlot, winWidth, winHeight);
		}
		// HZB runs in both configurations now: its depth blit, binds and readback are all commands
		// (the readback is ReadPixelsToBuffer + a DownloadBuffer fetch once the fence is signalled,
		// so the frame never waits for the GPU).
		TraceView("BeginFrame");
		m_renderer->BeginFrame(frameDescriptor);
		TraceView("DrawFrame");
		DrawFrame();
		TraceView("EndFrame");
		m_renderer->EndFrame();
		TraceView("CopyFramebufferColor");
		m_driver->CopyFramebufferColor(m_msaaframebuffer, 0, m_framebuffer, 0);
		TraceView("PresentTo");
		const uint32_t slot = static_cast<uint32_t>(mSubmittedFrames % kPresentSlotCount);
		m_renderer->PresentTo(m_framebuffer, *m_presentTargets[static_cast<size_t>(slot)]);

		// The overlays are part of the frame the window pastes: ImGui, the 2D draw list and the
		// gizmos. PresentTo left the slot bound, so drawing them here puts them in the picture the
		// window shows and not in a target nobody reads. The callback only records commands (see
		// ViewportFrame::Render), so it is safe on the side that has no context as well.
		if (m_drawOverlays)
		{
			m_drawOverlays();
		}

		// The frame's last command: when it runs, every command this frame recorded has run. The
		// window pastes the slot it names and never waits for this side (filament's FFence is the
		// same signal carried by the command stream).
		auto commitView = [this, slot]() {
			this->Commit(slot);
		};
		m_driver->Enqueue(commitView);
		TraceView("flush");
		m_driver->Flush();
		if (!m_driver->IsRenderThreadActive())
		{
			m_driver->Execute();
		}
		++mSubmittedFrames;
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
void Editor::Panels::AView::Commit(uint32_t p_slot)
{
	mPresentedSlot.store(p_slot, std::memory_order_release);
}

uint32_t Editor::Panels::AView::GetFrameTextureID()
{
	auto color = PresentedTarget().GetAttachment<::Rendering::HAL::Texture>(
		::Rendering::Settings::EFramebufferAttachment::COLOR);
	return color.has_value() ? static_cast<uint32_t>(color.value().GetID()) : 0u;
}

::Rendering::Data::Framebuffer& Editor::Panels::AView::PresentedTarget()
{
	const uint32_t slot = mPresentedSlot.load(std::memory_order_acquire) % kPresentSlotCount;
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
