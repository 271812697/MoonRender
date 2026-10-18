#include <tracy/Tracy.hpp>
#include <algorithm>
#include <Core/Rendering/FramebufferUtil.h>
#include <Rendering/View/AView.h>
#include <Rendering/HAL/Profiling.h>
#include "Rendering/HAL/Common/TFramebuffer.h"
#include "Rendering/HAL/Common/TTexture.h"
#include <Rendering/Context/IGLContextHost.h>
#include <Rendering/Context/Driver.h>
#include <iostream>

Editor::Panels::AView::AView
(
	const std::string& p_title,
	::Rendering::Context::Driver& p_driver) :
	m_driver(&p_driver), m_framebuffer(p_title), m_presentTarget(p_title + "/Present"), name(p_title)
{
	::Core::Rendering::FramebufferUtil::SetupFramebuffer(
		m_framebuffer,
		static_cast<uint32_t>(1),
		static_cast<uint32_t>(1),
		true, true, false,false
	);
	::Core::Rendering::FramebufferUtil::SetupFramebuffer(
		m_msaaframebuffer,
		static_cast<uint32_t>(1),
		static_cast<uint32_t>(1),
		true, true, false, true
	);

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
	::Core::Rendering::FramebufferUtil::SetupFramebuffer(m_presentTarget, presentDesc, false, false);
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
	{
		FrameMarkStart(name.c_str());

		m_framebuffer.Resize(winWidth, winHeight);
		m_msaaframebuffer.Resize(winWidth,winHeight);

		InitFrame();

		Rendering::Data::FrameDescriptor frameDescriptor;
		frameDescriptor.renderWidth = winWidth;
		frameDescriptor.renderHeight = winHeight;
		frameDescriptor.camera = camera;
		frameDescriptor.outputMsaaBuffer = m_msaaframebuffer;
		frameDescriptor.presentBuffer = m_framebuffer;

		m_renderer->BeginFrame(frameDescriptor);
		DrawFrame();
		m_renderer->EndFrame();
		::Core::Rendering::FramebufferUtil::CopyFramebufferColor(m_msaaframebuffer, 0, m_framebuffer, 0);

		// Compose what leaves the module: the present pass turns the HDR frame into the RGBA8
		// image the window pastes. It runs here, on the module's context, because the quad and
		// its material are the module's objects.
		m_presentTarget.Resize(winWidth, winHeight);
		m_presentTarget.Bind();
		m_driver->SetViewport(0, 0, winWidth, winHeight);
		Present();

		// The overlays are part of the frame the window pastes: ImGui, the 2D draw list, the
		// gizmos. Drawn here, into the same target, on the same context - which is what keeps
		// them out of the window layer, and the module free of it.
		if (m_drawOverlays)
		{
			m_drawOverlays();
		}

		m_presentTarget.Unbind();

		FrameMarkEnd(name.c_str());
		m_driver->OnFrameCompleted();
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



