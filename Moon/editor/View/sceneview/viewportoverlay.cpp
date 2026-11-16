#include "viewportoverlay.h"

#include "renderer/SceneView.h"
#include "renderer/GizmoRenderPass.h"

#include <Core/Rendering/GbufferPass.h>
#include <Core/Rendering/HzbBuildPass.h>
#include <Core/Rendering/EngineBufferRenderFeature.h>
#include <Rendering/Features/FrameInfoRenderFeature.h>
#include <Rendering/Overlay/Im2DRender.h>

#include "Interactive/Im3DRenderer.h"
#include "Settings/DebugSetting.h"
#include "core/log.h"

#include <ThirdParty/imgui/imgui/imgui.h>

#include <cstdio>
#include <cstdlib>

namespace MOON
{
	bool ViewportOverlay::s_clipDemoApplied = false;

	ViewportOverlay::ViewportOverlay() = default;

	void ViewportOverlay::BeginFrame(Editor::Panels::SceneView& p_view)
	{
		// Raw per-frame FPS / frame time, measured between consecutive frames and shown
		// without any smoothing.
		const auto now = std::chrono::steady_clock::now();
		if (m_hasLastFrame)
		{
			const double frameMs = std::chrono::duration<double, std::milli>(now - m_lastFrame).count();
			if (frameMs > 0.0)
			{
				m_fps = 1000.0 / frameMs;
				m_frameMs = frameMs;
			}
		}
		m_lastFrame = now;
		m_hasLastFrame = true;

		// ImGui's and the 2D draw list's frames are opened here, on the module's context:
		// Im2DRender::newFrame creates its shader and vertex array on first use, and those
		// belong to the context that draws with them.
		ImRenderer::instance().newImgui();
		Rendering::Overlay::Im2DRender::Instance().NewFrame();
		// The 3D overlay (the view cube, the rotation centre) is drawn in the same frame
		// when its pass is on; the pass is what decides.
		if (p_view.GetRenderer().GetPass<Editor::Rendering::GizmoRenderPass>("ImRenderer").IsEnabled())
		{
			ImRenderer::instance().newFrame(&p_view);
		}
	}

	void ViewportOverlay::EndFrame(Editor::Panels::SceneView& p_view)
	{
		DrawGbufferViews(p_view);
		DrawFrameStats(p_view);

		const auto [overlayWidth, overlayHeight] = p_view.GetSafeSize();
		Rendering::Overlay::Im2DRender::Instance().EndFrame(overlayWidth, overlayHeight);
		ImRenderer::instance().endImgui();
	}

	void ViewportOverlay::DrawGbufferViews(Editor::Panels::SceneView& p_view)
	{
		if (!DebugSettings::instance().getNode("DebugImgui")->getData<bool>())
		{
			return;
		}

		const auto [viewWidth, viewHeight] = p_view.GetSafeSize();
		const ImVec2 a = { 0, 1 }, b = { 1, 0 };
		const ImVec2 size = ImVec2(static_cast<float>(viewWidth), static_cast<float>(viewHeight));
		auto& gbufferData = p_view.GetRenderer().GetPass<::Core::Rendering::GbufferPass>("Gbuffer").GetGbufferData();
		ImGui::Image(gbufferData.position->GetID(), size, a, b);
		ImGui::Image(gbufferData.normal->GetID(), size, a, b);
		ImGui::Image(gbufferData.occlusion->GetID(), size, a, b);
		ImGui::Image(gbufferData.occlusionBlur->GetID(), size, a, b);
	}

	void ViewportOverlay::DrawFrameStats(Editor::Panels::SceneView& p_view)
	{
		if (!DebugSettings::instance().getOrDefault<bool>("showFPS", false))
		{
			return;
		}

		const auto& frameInfo =
			p_view.GetRenderer().GetFeature<Rendering::Features::FrameInfoRenderFeature>().GetFrameInfo();
		const auto& hzb = p_view.GetRenderer().GetHzbStats();
		const auto& hzbPass = p_view.GetRenderer().GetPass<Core::Rendering::HzbBuildPass>("HZB");
		char text[1536];
		sprintf_s(text, sizeof(text),
			"FPS %.1f\n"
			"Frame %.2f ms\n"
			"Total vertex Count %llu\n"
			"Batch Triangle Count %llu\n"
			"Triangle Count %llu\n"
			"Triangle Vertex Count %llu\n"
			"Triangle instance Count %llu\n"
			"Batch line Count %llu\n"
			"line Count %llu\n"
			"line Vertex Count %llu\n"
			"line instance Count %llu\n"
			"[HZB] grid %ux%u depth[min %.6f max %.6f mean %.6f]\n"
			"[HZB] bvh instances %u | visited %u | culled nodes %u | occluded meshes %u\n"
			"[HZB] tests: occluded %u | bias rejected %u | bg rejected %u | best margin %.6f | bias %.6f\n"
			"[HZB] skipped drawables %u | cull %.3f ms\n"
			"[HZB] readback slots %u (pending %u) | skipped frames %u | latency %u frames\n",
			m_fps, m_frameMs,
			(unsigned long long)frameInfo.vertexCount,
			(unsigned long long)frameInfo.batchPolyCount,
			(unsigned long long)frameInfo.polyCount,
			(unsigned long long)frameInfo.vertexPolyCount,
			(unsigned long long)frameInfo.instancePolyCount,
			(unsigned long long)frameInfo.batchLineCount,
			(unsigned long long)frameInfo.lineCount,
			(unsigned long long)frameInfo.vertexLineCount,
			(unsigned long long)frameInfo.instancelineCount,
			hzb.gridWidth, hzb.gridHeight,
			hzb.gridMinDepth, hzb.gridMaxDepth, hzb.gridMeanDepth,
			hzb.bvhInstances, hzb.visitedNodes, hzb.culledNodes, hzb.occludedInstances,
			hzb.occludedNodeTests, hzb.biasRejectedNodes, hzb.backgroundRejectedNodes,
			hzb.bestMargin, p_view.GetRenderer().GetHzbCuller().GetDepthBias(),
			p_view.GetRenderer().GetHzbSkippedDrawables(),
			hzb.cullTimeMs,
			hzbPass.GetReadbackSlotCount(),
			hzbPass.GetReadbackPendingSlots(),
			hzbPass.GetReadbackSkippedFrames(),
			hzbPass.GetLastReadbackLatencyFrames()
		);
		ImGui::GetForegroundDrawList()->AddText({ 20,20 }, IM_COL32(255, 255, 100, 255), text);
	}

	void ViewportOverlay::ApplyClipDemo(Editor::Panels::SceneView& p_view)
	{
		if (s_clipDemoApplied)
		{
			return;
		}
		const char* p_ratio = std::getenv("MOON_CLIP_DEMO");
		if (p_ratio == nullptr || *p_ratio == '\0')
		{
			return;
		}
		auto* scene = p_view.GetScene();
		if (scene == nullptr)
		{
			return;
		}
		const auto box = scene->GetSceneBoundingBox();
		if (!box.isValid())
		{
			return;
		}

		const float ratio = static_cast<float>(std::atof(p_ratio));
		const Maths::FVector3 center = box.center();
		const float height = box.lengthZ();
		const Maths::FVector3 normal{ 0.0f, 0.0f, 1.0f };
		const float d = center.z + ratio * height * 0.5f;

		auto& feature = p_view.GetRenderer().GetFeature<::Core::Rendering::EngineBufferRenderFeature>();
		feature.SetClipPlane(normal.x, normal.y, normal.z, -d);
		feature.EnableClip(true);
		s_clipDemoApplied = true;
		CORE_INFO("[Section] clip demo plane z = {0} (ratio {1}, bbox height {2})", d, ratio, height);
	}
}
