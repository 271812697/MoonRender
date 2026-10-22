#include <Core/Rendering/HzbBuildPass.h>

#include <algorithm>
#include <chrono>

#include <Core/Global/ServiceLocator.h>
#include <Core/Rendering/FramebufferUtil.h>
#include <Core/ResourceManagement/ShaderManager.h>
#include <Rendering/Core/CompositeRenderer.h>
#include <Rendering/HAL/Profiling.h>
#include <Rendering/HAL/Texture.h>
#include <Rendering/Settings/EBlitFlags.h>
#include <Rendering/Settings/TextureDesc.h>

namespace
{
	using namespace ::Rendering::Settings;

	/** R32F single channel target used for every pyramid level. */
	TextureDesc MakeGridTextureDesc(uint32_t p_width, uint32_t p_height)
	{
		TextureDesc desc{};
		desc.width = std::max(1u, p_width);
		desc.height = std::max(1u, p_height);
		desc.minFilter = ETextureFilteringMode::NEAREST;
		desc.magFilter = ETextureFilteringMode::NEAREST;
		desc.horizontalWrap = ETextureWrapMode::CLAMP_TO_EDGE;
		desc.verticalWrap = ETextureWrapMode::CLAMP_TO_EDGE;
		desc.internalFormat = EInternalFormat::R32F;
		desc.useMipMaps = false;
		desc.mutableDesc = MutableTextureDesc{
			.format = EFormat::RED,
			.type = EPixelDataType::FLOAT
		};
		return desc;
	}

	/** Depth only source: the resolved (single sampled) opaque scene depth. */
	TextureDesc MakeDepthTextureDesc(uint32_t p_width, uint32_t p_height)
	{
		TextureDesc desc{};
		desc.width = std::max(1u, p_width);
		desc.height = std::max(1u, p_height);
		desc.minFilter = ETextureFilteringMode::NEAREST;
		desc.magFilter = ETextureFilteringMode::NEAREST;
		desc.horizontalWrap = ETextureWrapMode::CLAMP_TO_EDGE;
		desc.verticalWrap = ETextureWrapMode::CLAMP_TO_EDGE;
		desc.internalFormat = EInternalFormat::DEPTH_COMPONENT32F;
		desc.useMipMaps = false;
		desc.mutableDesc = MutableTextureDesc{
			.format = EFormat::DEPTH_COMPONENT,
			.type = EPixelDataType::FLOAT
		};
		return desc;
	}
}

Core::Rendering::HzbBuildPass::HzbBuildPass(::Rendering::Core::CompositeRenderer& p_renderer)
	: ::Rendering::Core::ARenderPass(p_renderer)
{
	m_reduceMaterial.SetShader(GetShaderService[":Shaders\\PostProcess\\HzbReduce.shader"]);
	m_reduceMaterial.SetDepthTest(false);
	m_reduceMaterial.SetDepthWriting(false);
	m_reduceMaterial.SetColorWriting(true);
	m_reduceMaterial.SetBlendable(false);
	m_reduceMaterial.SetBackfaceCulling(false);
	m_reduceMaterial.SetFrontfaceCulling(false);

	SetupTargets(1, 1);
}

Core::Rendering::HzbBuildPass::~HzbBuildPass()
{
	ReleaseReadbacks();
}

void Core::Rendering::HzbBuildPass::ResizeRenderer(int p_width, int p_height)
{
	// Targets are rebuilt lazily in Draw() when the frame size changes.
	m_targetsReady = false;
}

void Core::Rendering::HzbBuildPass::ReleaseLevels()
{
	m_levels.clear();
	m_levelResolutions.clear();
	m_levelTextures.clear();
	m_gridWidth = 0;
	m_gridHeight = 0;
	m_targetsReady = false;
}

void Core::Rendering::HzbBuildPass::SetupTargets(uint32_t p_width, uint32_t p_height)
{
	p_width = std::max(1u, p_width);
	p_height = std::max(1u, p_height);

	m_resolveColor = std::make_shared<::Rendering::HAL::Texture>(
		ETextureType::TEXTURE_2D,
		"Hzb/ResolveColor"
	);
	m_resolveColor->Allocate(MakeGridTextureDesc(p_width, p_height));

	m_resolveDepth = std::make_shared<::Rendering::HAL::Texture>(
		ETextureType::TEXTURE_2D,
		"Hzb/ResolveDepth"
	);
	m_resolveDepth->Allocate(MakeDepthTextureDesc(p_width, p_height));

	m_resolveFbo.Attach<::Rendering::HAL::Texture>(
		m_resolveColor,
		EFramebufferAttachment::COLOR
	);
	m_resolveFbo.Attach<::Rendering::HAL::Texture>(
		m_resolveDepth,
		EFramebufferAttachment::DEPTH
	);
	m_resolveFbo.Validate();

	m_levels.clear();
	m_levelResolutions.clear();
	m_levelTextures.clear();

	// Half resolution per level until the grid is small enough to read back
	// cheaply. Each level stores the max depth of the 2x2 block below it.
	uint32_t levelWidth = p_width;
	uint32_t levelHeight = p_height;
	constexpr int kMaxLevelCount = 16;

	for (int i = 0; i < kMaxLevelCount; ++i)
	{
		if (levelWidth <= m_maxGridSize && levelHeight <= m_maxGridSize)
		{
			break;
		}

		levelWidth = std::max(1u, levelWidth / 2);
		levelHeight = std::max(1u, levelHeight / 2);

		auto texture = std::make_shared<::Rendering::HAL::Texture>(
			ETextureType::TEXTURE_2D,
			std::format("Hzb/Level{}", i)
		);
		texture->Allocate(MakeGridTextureDesc(levelWidth, levelHeight));

		auto framebuffer = std::make_unique<::Rendering::HAL::Framebuffer>(
			std::format("HzbLevel{}", i)
		);
		framebuffer->Attach<::Rendering::HAL::Texture>(
			texture,
			EFramebufferAttachment::COLOR
		);
		framebuffer->Validate();
		m_levels.push_back(std::move(framebuffer));

		m_levelTextures.push_back(texture);
		m_levelResolutions.emplace_back(
			static_cast<float>(levelWidth),
			static_cast<float>(levelHeight)
		);
	}

	m_gridWidth = m_levels.empty() ? 0 : static_cast<uint32_t>(levelWidth);
	m_gridHeight = m_levels.empty() ? 0 : static_cast<uint32_t>(levelHeight);
	m_width = p_width;
	m_height = p_height;
	m_targetsReady = true;

	SetupReadbacks();
}

void Core::Rendering::HzbBuildPass::SetupReadbacks()
{
	ReleaseReadbacks();

	if (m_gridWidth == 0 || m_gridHeight == 0)
	{
		return;
	}

	m_readbackBytes = static_cast<uint64_t>(m_gridWidth) * m_gridHeight * sizeof(float);
	m_readbackSlots.resize(kReadbackSlotCount);

	for (ReadbackSlot& slot : m_readbackSlots)
	{
		// PIXEL_PACK buffer: destination of the asynchronous glReadPixels. The
		// STREAM_READ hint matches "written once by the GPU, read once by the CPU".
		slot.buffer = std::make_shared<::Rendering::HAL::Buffer>(
			::Rendering::Settings::EBufferType::PIXEL_PACK
		);
		slot.buffer->Allocate(
			m_readbackBytes,
			::Rendering::Settings::EAccessSpecifier::STREAM_READ
		);
	}
}

void Core::Rendering::HzbBuildPass::ReleaseReadbacks()
{
	for (ReadbackSlot& slot : m_readbackSlots)
	{
		if (slot.buffer)
		{
			slot.buffer->ClearFence();
		}
		slot.buffer.reset();
		slot.pending = false;
	}

	m_readbackSlots.clear();
	m_readbackPendingSlots = 0;
	m_readbackWriteIndex = 0;
	m_readbackBytes = 0;
}

void Core::Rendering::HzbBuildPass::ConsumeReadyReadback()
{
	if (m_readbackSlots.empty() || m_culler == nullptr)
	{
		return;
	}

	const uint32_t slotCount = static_cast<uint32_t>(m_readbackSlots.size());

	// Oldest slot first: it is the one the next submission would overwrite.
	for (uint32_t i = 0; i < slotCount; ++i)
	{
		const uint32_t index = (m_readbackWriteIndex + i) % slotCount;
		ReadbackSlot& slot = m_readbackSlots[index];

		if (!slot.pending || !slot.buffer->IsFenceSignaled())
		{
			continue;
		}

		void* mapped = slot.buffer->MapRead(0, m_readbackBytes);
		if (mapped == nullptr)
		{
			// Could not map (the GPU is still using the buffer): retry next frame.
			break;
		}

		const float* depths = static_cast<const float*>(mapped);
		const size_t count = static_cast<size_t>(m_gridWidth) * m_gridHeight;

		// A slot that was never written still holds zeros, which in reversed-Z
		// means "far": the culler then simply finds nothing occluded.
		m_culler->SetGrid(m_gridWidth, m_gridHeight, std::vector<float>(depths, depths + count));

		slot.buffer->Unmap();
		slot.buffer->ClearFence();
		slot.pending = false;
		if (m_readbackPendingSlots != 0)
		{
			--m_readbackPendingSlots;
		}
		m_lastReadbackLatencyFrames = static_cast<uint32_t>(m_frameCounter - slot.issueFrame);
		break;
	}
}

void Core::Rendering::HzbBuildPass::IssueReadback()
{
	if (m_readbackSlots.empty() || m_levels.empty()
		|| m_gridWidth == 0 || m_gridHeight == 0)
	{
		return;
	}

	ReadbackSlot& slot = m_readbackSlots[m_readbackWriteIndex];

	if (slot.pending)
	{
		// The GPU is a full ring behind: skip this frame instead of blocking the
		// CPU; the culler keeps using the previous grid.
		++m_readbackSkippedFrames;
	}
	else
	{
		m_levels.back()->ReadPixelsToBuffer(
			*slot.buffer,
			0,
			0,
			0,
			m_gridWidth,
			m_gridHeight,
			::Rendering::Settings::EPixelDataFormat::RED,
			::Rendering::Settings::EPixelDataType::FLOAT
		);
		slot.buffer->InsertFence();
		slot.pending = true;
		slot.issueFrame = m_frameCounter;
		++m_readbackPendingSlots;
	}

	m_readbackWriteIndex
		= (m_readbackWriteIndex + 1) % static_cast<uint32_t>(m_readbackSlots.size());
}

void Core::Rendering::HzbBuildPass::Draw(::Rendering::Data::PipelineState p_pso)
{
	ZoneScoped;
	TracyGpuZone("HzbBuildPass");

	const auto& frameDescriptor = m_renderer.GetFrameDescriptor();
	if (!frameDescriptor.outputMsaaBuffer.has_value() || m_culler == nullptr)
	{
		return;
	}

	auto& msaaBuffer = frameDescriptor.outputMsaaBuffer.value();
	const auto [frameWidth, frameHeight] = msaaBuffer.GetSize();
	if (frameWidth == 0 || frameHeight == 0)
	{
		return;
	}

	if (!m_targetsReady || frameWidth != m_width || frameHeight != m_height)
	{
		SetupTargets(frameWidth, frameHeight);
	}

	if (m_levels.empty())
	{
		return;
	}

	const auto startTime = std::chrono::high_resolution_clock::now();

	// 1) Resolve the MSAA depth into a sampleable depth texture. This is the
	//    opaque (+ section cap) depth: transparents are depth peeled into their
	//    own layer framebuffers and never occlude anything.
	::Core::Rendering::FramebufferUtil::CopyFramebufferDepth(msaaBuffer, m_resolveFbo);

	// 2) First reduce: depth texture -> level 0.
	{
		const auto [levelWidth, levelHeight] = m_levels[0]->GetSize();
		m_reduceMaterial.SetProperty(
			"_InputResolution",
			Maths::FVector2{
				static_cast<float>(m_width),
				static_cast<float>(m_height)
			},
			true
		);

		m_levels[0]->Bind();
		m_renderer.SetViewport(0, 0, levelWidth, levelHeight);
		m_renderer.Present(*m_resolveDepth, m_reduceMaterial);
		m_levels[0]->Unbind();
	}

	// 3) Remaining levels: R32F color -> half resolution R32F color.
	for (size_t i = 1; i < m_levels.size(); ++i)
	{
		m_reduceMaterial.SetProperty("_InputResolution", m_levelResolutions[i - 1], true);
		m_renderer.Blit(
			p_pso,
			*m_levels[i - 1],
			*m_levels[i],
			m_reduceMaterial,
			::Rendering::Settings::EBlitFlags::FILL_INPUT_TEXTURE
				| ::Rendering::Settings::EBlitFlags::UPDATE_VIEWPORT_SIZE
		);
	}

	// 4) Asynchronous readback through a PBO ring: the smallest level is copied
	//    into a pixel pack buffer now and fetched a few frames later, once its
	//    fence is already signalled, so the CPU never waits for the GPU (a
	//    synchronous glReadPixels would stall the pipeline every frame). The
	//    culler therefore consumes a grid that is 2~3 frames old.
	ConsumeReadyReadback();
	IssueReadback();
	++m_frameCounter;

	// Restore the frame's default target for the following passes.
	msaaBuffer.Bind();

	m_lastBuildTimeMs = std::chrono::duration<float, std::milli>(
		std::chrono::high_resolution_clock::now() - startTime
	).count();
}
