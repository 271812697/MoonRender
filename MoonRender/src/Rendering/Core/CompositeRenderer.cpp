#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <functional>
#include <ranges>
#include <string>
#include <vector>
#include <tracy/Tracy.hpp>
#include <Core/Rendering/SkyBoxRenderPass .h>
#include <Rendering/Backend/ReadPixelsRequest.h>
#include <Rendering/Core/CompositeRenderer.h>
#include <Rendering/HAL/Profiling.h>
#include <Rendering/Utils/FrameTrace.h>

namespace
{
	/** MOON_FRAME_TRACE prints each pass as it starts: the bring-up tool for finding the pass that
	 * blocks when the render thread owns the context. */
	void TracePass(const char* p_name)
	{
		static const bool enabled = std::getenv("MOON_FRAME_TRACE") != nullptr;
		if (!enabled)
		{
			return;
		}
		std::fprintf(stderr, "[pass] %s\n", p_name);
		std::fflush(stderr);
	}
}
Rendering::Core::CompositeRenderer::CompositeRenderer(Context::Driver& p_driver)
	: ABaseRenderer(p_driver)
{
}
::Rendering::HAL::Texture* Rendering::Core::CompositeRenderer::GetSkyBoxCube() {
	return GetPass<::Core::Rendering::SkyboxRenderPass>("SkyboxRenderPass").GetSkyBoxCube();;
}
::Rendering::HAL::Texture* Rendering::Core::CompositeRenderer::GetIrradianceCube() {
	return GetPass<::Core::Rendering::SkyboxRenderPass>("SkyboxRenderPass").GetIrradianceCube();;
}
::Rendering::HAL::Texture* Rendering::Core::CompositeRenderer::GetPrefilterCube() {
	return GetPass<::Core::Rendering::SkyboxRenderPass>("SkyboxRenderPass").GetPrefilterCube();;
}
::Rendering::HAL::Texture* Rendering::Core::CompositeRenderer::GetBrdfTexture() {
	return GetPass<::Core::Rendering::SkyboxRenderPass>("SkyboxRenderPass").GetBrdfTexture();;
}
void Rendering::Core::CompositeRenderer::BeginFrame(const Data::FrameDescriptor& p_frameDescriptor)
{
	ZoneScoped;
	TracyGpuZone("BeginFrame");

	TracePass("BeginFrame: base");
	ABaseRenderer::BeginFrame(p_frameDescriptor);
	TracePass("BeginFrame: features");

	for (const auto& feature : m_features | std::views::values)
	{
		if (feature->IsEnabled())
		{
			feature->OnBeginFrame(p_frameDescriptor);
		}
	}

	for (const auto& pass : m_passes | std::views::values)
	{
		if (pass.second->IsEnabled())
		{
			pass.second->OnBeginFrame(p_frameDescriptor);
		}
	}
}

void Rendering::Core::CompositeRenderer::DrawFrame()
{
	ZoneScoped;
	TracyGpuZone("DrawFrame");
	// The DFG LUT is a view-global texture; materials declare _BRDFLut but must not carry it as
	// a per-instance property (a material without it sampled the white empty texture and lost
	// its whole diffuse term).
	SetBrdfLut(GetBrdfTexture());
	// Filament's IndirectLight: the sky pass bakes the irradiance and prefiltered cubes, and every
	// lit material reads them. They are view state, not material parameters, so they live in the
	// per-view set owned by the engine rather than in each material instance.
	SetEnvironmentTextures(GetIrradianceCube(), GetPrefilterCube());
	// The per-view set now owns EngineUBO, ReflectionUBO and the three IBL textures; bind its
	// fixed slots once for the whole frame.
	BindPerViewDescriptorSet();
	auto pso = CreatePipelineState();
	// MOON_PASS_ONLY=<name> runs a single pass: the bring-up tool for finding which one leaves
	// the render targets in a state the next one cannot use.
	static const char* onlyPass = std::getenv("MOON_PASS_ONLY");

	// MOON_PASS_DUMP_DIR=<dir>: writes the frame's colour target after every pass to
	// <dir>/<pass>.raw (RGBA8, renderWidth x renderHeight), a few times a second. Comparing those
	// between two modes finds the first pass whose output differs, which is the bisect that decides
	// where a shading difference is introduced.
	static const char* dumpDir = std::getenv("MOON_PASS_DUMP_DIR");
	static int frameCounter = 0;
	const bool dumpThisFrame = dumpDir != nullptr && (frameCounter % 120) == 0;
	++frameCounter;

	for (const auto& pass : m_passes | std::views::values)
	{
		if (onlyPass != nullptr && pass.first != onlyPass)
		{
			continue;
		}
		// MOON_NO_GBUFFER: bisect switch - the G-buffer pass draws the same drawables into its own
		// targets, and whatever it leaves behind (depth, state) is a candidate for a shading
		// difference that no draw-state dump can show.
		if (std::getenv("MOON_NO_GBUFFER") != nullptr && pass.first == "Gbuffer")
		{
			continue;
		}
		// MOON_SKIP_PASSES=Name,Name: leave passes out of the frame. Comparing the two modes with a
		// set of passes gone is how the pass that introduces a difference is found.
		static const char* const skipPasses = std::getenv("MOON_SKIP_PASSES");
		if (skipPasses != nullptr && !pass.first.empty())
		{
			const std::string skipList(skipPasses);
			std::string needle = "," + std::string(pass.first) + ",";
			std::string haystack = "," + skipList + ",";
			if (haystack.find(needle) != std::string::npos)
			{
				continue;
			}
		}
		TracePass(pass.first.c_str());
		// The executor's traces ([fbo], [dm]) do not know which pass asked for them; the label
		// published here is what they print (Rendering/Utils/FrameTrace.h).
		::Rendering::Utils::FrameTrace::SetPass(pass.first.c_str());
		m_currentPass = pass.second.get();

		m_driver.BindFramebuffer(&m_frameDescriptor.outputMsaaBuffer.value());
		SetViewport(0, 0, m_frameDescriptor.renderWidth, m_frameDescriptor.renderHeight);

		if (m_currentPass->IsEnabled())
		{
			TracePass("  draw");
			m_currentPass->Draw(pso);
			TracePass("  done");
		}

		// MOON_PASS_PROBE: does this pass leave anything in the frame's colour target? An MSAA
		// target cannot be read back, so it is blitted into the present target first - the present
		// pass overwrites that later in the frame, so the probe costs the frame nothing. The centre
		// pixel per pass is what localises "the frame loses its content here".
		//
		// The value says which half runs, which is how a bring-up mystery is bisected: "c" only the
		// blit, "r" only the readback, "d" only the drain, anything else all three.
		static const char* probeMode = std::getenv("MOON_PASS_PROBE");
		// Not gated on the render thread: comparing the two modes pass by pass is what the probe is
		// for, and it is already opt-in through the environment variable.
		if (probeMode != nullptr
			&& m_frameDescriptor.outputMsaaBuffer.has_value()
			&& m_frameDescriptor.presentBuffer.has_value())
		{
			const bool withCopy = std::strchr(probeMode, 'c') != nullptr || probeMode[0] == '\0'
				|| (std::strchr(probeMode, 'r') == nullptr && std::strchr(probeMode, 'd') == nullptr);
			const bool withRead = std::strchr(probeMode, 'r') != nullptr
				|| (std::strchr(probeMode, 'c') == nullptr && std::strchr(probeMode, 'd') == nullptr);
			const bool withDrain = std::strchr(probeMode, 'd') != nullptr || withCopy || withRead;

			HAL::Framebuffer& source = m_frameDescriptor.outputMsaaBuffer.value();
			HAL::Framebuffer& scratch = m_frameDescriptor.presentBuffer.value();
			if (withCopy)
			{
				m_driver.CopyFramebufferColor(source, 0, scratch, 0);
			}

			if (withRead)
			{
				auto pixels = std::make_shared<std::vector<uint8_t>>(4);
				::Rendering::Backend::ReadPixelsRequest request;
				request.framebufferId = scratch.GetID();
				request.readBufferIndex = 0;
				request.x = static_cast<uint32_t>(m_frameDescriptor.renderWidth) / 2;
				request.y = static_cast<uint32_t>(m_frameDescriptor.renderHeight) / 2;
				request.width = 1;
				request.height = 1;
				request.format = ::Rendering::Settings::EPixelDataFormat::RGBA;
				request.type = ::Rendering::Settings::EPixelDataType::UNSIGNED_BYTE;
				request.data = pixels;
				const std::string label(pass.first);
				request.onDone = [pixels, label] {
					std::fprintf(stderr, "[passprobe] %s centre = %u %u %u %u\n", label.c_str(),
						(*pixels)[0], (*pixels)[1], (*pixels)[2], (*pixels)[3]);
					std::fflush(stderr);
				};
				m_driver.ReadPixels(request);
			}

			if (withDrain)
			{
				m_driver.ExecutePendingCommands();
			}
		}

		if (dumpThisFrame && m_frameDescriptor.outputMsaaBuffer.has_value()
			&& m_frameDescriptor.presentBuffer.has_value())
		{
			// Whole-target readback of the present buffer (the MSAA target was just blitted into
			// it): one file per pass, so the first pass that differs between two modes is a diff
			// away instead of a hypothesis.
			const uint32_t width = static_cast<uint32_t>(m_frameDescriptor.renderWidth);
			const uint32_t height = static_cast<uint32_t>(m_frameDescriptor.renderHeight);
			m_driver.CopyFramebufferColor(m_frameDescriptor.outputMsaaBuffer.value(), 0,
				m_frameDescriptor.presentBuffer.value(), 0);
			auto pixels = std::make_shared<std::vector<uint8_t>>(
				static_cast<size_t>(width) * height * 4, 0);
			::Rendering::Backend::ReadPixelsRequest request;
			request.framebufferId = m_frameDescriptor.presentBuffer.value().GetID();
			request.readBufferIndex = 0;
			request.x = 0;
			request.y = 0;
			request.width = width;
			request.height = height;
			request.format = ::Rendering::Settings::EPixelDataFormat::RGBA;
			request.type = ::Rendering::Settings::EPixelDataType::UNSIGNED_BYTE;
			request.data = pixels;
			const std::string label(pass.first);
			const std::string directory(dumpDir);
			request.onDone = [pixels, label, directory] {
				const std::string path = directory + "\\" + label + ".raw";
				if (FILE* file = std::fopen(path.c_str(), "wb"))
				{
					std::fwrite(pixels->data(), 1, pixels->size(), file);
					std::fclose(file);
				}
			};
			m_driver.ReadPixels(request);
			m_driver.ExecutePendingCommands();
		}

		m_currentPass.reset();
	}
}

void Rendering::Core::CompositeRenderer::PresentTo(
	::Rendering::HAL::Framebuffer& p_source,
	::Rendering::HAL::Framebuffer& p_target)
{
	if (m_presentPass == nullptr)
	{
		m_presentPass = std::make_unique<PresentPass>(*this);
	}

	// Everything the present needs is stated here: the pass it belongs to (so the feature hooks can
	// match it), the target, the viewport, and the source texture. Nothing is inherited from the
	// passes that ran before, which is what makes recording this call safe.
	m_currentPass = m_presentPass.get();
	m_driver.BindFramebuffer(&p_target);
	const auto [width, height] = p_target.GetSize();
	SetViewport(0, 0, width, height);
	Present(p_source);
	m_currentPass.reset();
}

void Rendering::Core::CompositeRenderer::EndFrame()
{
	ZoneScoped;
	TracyGpuZone("EndFrame");

	for (const auto& pass : m_passes | std::views::values)
	{
		if (pass.second->IsEnabled())
		{
			pass.second->OnEndFrame();
		}
	}

	for (const auto& feature : m_features | std::views::values)
	{
		if (feature->IsEnabled())
		{
			feature->OnEndFrame();
		}
	}

	ClearDescriptors();
	ABaseRenderer::EndFrame();
}

void Rendering::Core::CompositeRenderer::DrawEntity(
	Rendering::Data::PipelineState p_pso,
	const Entities::Drawable& p_drawable
)
{
	ZoneScoped;

	// Ensure the drawable is valid.
	// If not, skip the draw call and the attached features.
	if (!IsDrawable(p_drawable))
	{
		return;
	}

	// A drawable drawn outside any pass - the present/ blit quad is one - has no pass to match
	// features against, and that is legal: the features are per-pass hooks (an outline pass, a
	// picking pass), not something every draw runs through. Saying so explicitly also keeps this
	// path usable from a queued render block, where there is no "current pass" at all.
	if (!m_currentPass.has_value())
	{
		ABaseRenderer::DrawEntity(p_pso, p_drawable);
		return;
	}

	const auto& currentPass = m_currentPass.value();
	const auto& passTypeId = typeid(currentPass);
	for (const auto& feature : m_features | std::views::values)
	{
		if (feature->IsEnabledFor(passTypeId))
		{
			feature->OnBeforeDraw(p_pso, p_drawable);
		}
	}

	ABaseRenderer::DrawEntity(p_pso, p_drawable);

	for (const auto& feature : m_features | std::views::values)
	{
		if (feature->IsEnabledFor(passTypeId))
		{
			feature->OnAfterDraw(p_drawable);
		}
	}
}
