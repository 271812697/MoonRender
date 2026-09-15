#include <ranges>
#include <Core/ECS/Components/CMaterialRenderer.h>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <Core/Rendering/EngineDrawableDescriptor.h>
#include <Core/Rendering/FramebufferUtil.h>
#include "Core/Global/ServiceLocator.h"
#include "DebugModelRenderFeature.h"
#include "DebugSceneRenderer.h"
#include "PickingRenderPass.h"
#include "Interactive/Im3DRenderer.h"
#include "Qtimgui/imgui/imgui.h"
#include <Rendering/HAL/Profiling.h>
#include <Rendering/Settings/EAccessSpecifier.h>
#include <Rendering/Settings/EBufferType.h>

namespace
{
	void PreparePickingMaterial(
		const Core::ECS::Actor& p_actor,
		Rendering::Data::Material& p_material,
		const std::string& p_uniformName = "_PickingColor"
	)
	{
		uint32_t actorID = static_cast<uint32_t>(p_actor.GetID());	
		auto bytes = reinterpret_cast<uint8_t*>(&actorID);
		auto color = Maths::FVector4{ bytes[0] / 255.0f, bytes[1] / 255.0f, bytes[2] / 255.0f, 1.0f };

		// Set the picking color property if it exists
		if (p_material.GetProperty(p_uniformName))
		{
			p_material.SetProperty(p_uniformName, color, true);
		}
	}
}

Editor::Rendering::PickingRenderPass::PickingRenderPass(::Rendering::Core::CompositeRenderer& p_renderer) :
	::Rendering::Core::ARenderPass(p_renderer),
	m_actorPickingFramebuffer("ActorPicking")
{
	::Core::Rendering::FramebufferUtil::SetupFramebuffer(
		m_actorPickingFramebuffer, 1, 1, true, false, false
	);

	SetupPickReadbacks();
	/* Picking Material */
	m_actorPickingFallbackMaterial.SetShader(GetShaderService[":Shaders/PickingFallback.ovfx"]);
	m_TopoShapePickingFallbackMaterial.SetShader(GetShaderService[":Shaders\\GeomertySurfacePick.ovfx"]);
	m_TopoShapePickingFallbackMaterial.SetBackfaceCulling(false);
	// Lines and faces are coplanar in the picking buffer too: push the faces
	// back in depth so the line IDs win over their own faces.
	m_TopoShapePickingFallbackMaterial.SetPolygonOffsetFill(true);
	m_TopoShapePickingFallbackMaterial.SetLineWidth(8);
}

Editor::Rendering::PickingRenderPass::PickingResult Editor::Rendering::PickingRenderPass::ReadbackPickingResult(
	const ::Core::SceneSystem::Scene& p_scene,
	uint32_t p_x,
	uint32_t p_y,bool& isSelected
)
{
	ZoneScoped;
	uint8_t pixel[4];
	m_actorPickingFramebuffer.ReadPixels(
		p_x, p_y, 1, 1,
		::Rendering::Settings::EPixelDataFormat::RGBA,
		::Rendering::Settings::EPixelDataType::UNSIGNED_BYTE,
		pixel
	);
	return DecodePickResult(p_scene, pixel, isSelected);
}

Editor::Rendering::PickingRenderPass::PickingResult Editor::Rendering::PickingRenderPass::DecodePickResult(
	const ::Core::SceneSystem::Scene& p_scene,
	const uint8_t p_pixel[4],
	bool& p_isSelected
)
{
	auto& gizmoInstance = MOON::ImRenderer::instance();

	if (p_pixel[3] == 255) {
		gizmoInstance.resetSelectPolygon();
		uint32_t actorID = (p_pixel[2] << 16) | (p_pixel[1] << 8) | (p_pixel[0] << 0);
		auto actorUnderMouse = p_scene.FindActorByID(actorID);

		if (actorUnderMouse)
		{
			return Tools::Utils::OptRef(*actorUnderMouse);
		}
		else if (
			p_pixel[0] == 255 &&
			p_pixel[1] == 255 &&
			p_pixel[2] >= 252 &&
			p_pixel[2] <= 254
			)
		{
			return static_cast<Editor::Core::GizmoBehaviour::EDirection>(p_pixel[2] - 252);
		}
		p_isSelected = true;
	}
	else if(p_pixel[3]==254)
	{
		uint32_t polygonID = p_pixel[2];
		uint32_t blockID = p_pixel[1];
		gizmoInstance.selectPolygon(polygonID,blockID);
		p_isSelected = true;
	}
	else
	{
		gizmoInstance.resetSelectPolygon();
		p_isSelected = false;
	}

	return std::nullopt;
}

void Editor::Rendering::PickingRenderPass::SetupPickReadbacks()
{
	m_pickReadbackSlots.clear();
	m_pickReadbackSlots.resize(kPickReadbackSlotCount);
	m_pickWriteIndex = 0;
	m_pickRequestPending = false;

	for (PickReadbackSlot& slot : m_pickReadbackSlots)
	{
		// PIXEL_PACK buffer: destination of the asynchronous glReadPixels.
		slot.buffer = std::make_shared<::Rendering::HAL::Buffer>(
			::Rendering::Settings::EBufferType::PIXEL_PACK
		);
		slot.buffer->Allocate(
			kPickReadbackBytes,
			::Rendering::Settings::EAccessSpecifier::STREAM_READ
		);
	}
}

void Editor::Rendering::PickingRenderPass::RequestPick(uint32_t p_x, uint32_t p_y)
{
	// Only the newest request matters: serving an older one would highlight a
	// position the cursor already left.
	m_pickRequestX = p_x;
	m_pickRequestY = p_y;
	m_pickRequestPending = true;
}

void Editor::Rendering::PickingRenderPass::IssuePickReadback()
{
	if (!m_pickRequestPending || m_pickReadbackSlots.empty())
	{
		m_pickRequestPending = false;
		return;
	}

	PickReadbackSlot* freeSlot = nullptr;
	for (uint32_t i = 0; i < kPickReadbackSlotCount; ++i)
	{
		PickReadbackSlot& slot = m_pickReadbackSlots[
			(m_pickWriteIndex + i) % kPickReadbackSlotCount
		];
		if (!slot.pending)
		{
			freeSlot = &slot;
			m_pickWriteIndex = (m_pickWriteIndex + i + 1) % kPickReadbackSlotCount;
			break;
		}
	}

	m_pickRequestPending = false;

	if (freeSlot == nullptr)
	{
		// The whole ring is still in flight: drop the request instead of
		// blocking. The hover highlight simply lags one more frame.
		return;
	}

	m_actorPickingFramebuffer.ReadPixelsToBuffer(
		*freeSlot->buffer,
		0,
		m_pickRequestX,
		m_pickRequestY,
		1, 1,
		::Rendering::Settings::EPixelDataFormat::RGBA,
		::Rendering::Settings::EPixelDataType::UNSIGNED_BYTE
	);
	freeSlot->buffer->InsertFence();
	freeSlot->x = m_pickRequestX;
	freeSlot->y = m_pickRequestY;
	freeSlot->pending = true;
}

bool Editor::Rendering::PickingRenderPass::TryConsumePick(
	const ::Core::SceneSystem::Scene& p_scene,
	PickingResult& p_outResult,
	uint32_t& p_outX,
	uint32_t& p_outY
)
{
	if (m_pickReadbackSlots.empty())
	{
		return false;
	}

	for (uint32_t i = 0; i < kPickReadbackSlotCount; ++i)
	{
		PickReadbackSlot& slot = m_pickReadbackSlots[
			(m_pickWriteIndex + i) % kPickReadbackSlotCount
		];
		if (!slot.pending || !slot.buffer->IsFenceSignaled())
		{
			continue;
		}

		void* mapped = slot.buffer->MapRead(0, kPickReadbackBytes);
		if (mapped == nullptr)
		{
			// Still used by the GPU: retry on the next frame.
			continue;
		}

		uint8_t pixel[4];
		std::memcpy(pixel, mapped, kPickReadbackBytes);
		slot.buffer->Unmap();
		slot.buffer->ClearFence();
		slot.pending = false;

		p_outX = slot.x;
		p_outY = slot.y;

		bool ignoredSelectionState = false;
		p_outResult = DecodePickResult(p_scene, pixel, ignoredSelectionState);
		return true;
	}

	return false;
}

void Editor::Rendering::PickingRenderPass::Draw(::Rendering::Data::PipelineState p_pso)
{
	ZoneScoped;
	TracyGpuZone("PickingRenderPass");

	// The picking target is a full second scene pass (RGBA8 + depth at render
	// resolution), so it is only rendered on demand: RequestPick() sets the flag
	// for hover / camera motion / clicks, and Idle frames skip it entirely.
	if (!m_pickRequestPending)
	{
		return;
	}

	using namespace ::Core::Rendering;

	assert(m_renderer.HasDescriptor<SceneRenderer::SceneDescriptor>()&&"Cannot find SceneDescriptor attached to this renderer");
	assert(m_renderer.HasDescriptor<DebugSceneRenderer::DebugSceneDescriptor>()&&"Cannot find DebugSceneDescriptor attached to this renderer");

	auto& sceneDescriptor = m_renderer.GetDescriptor<SceneRenderer::SceneDescriptor>();
	auto& debugSceneDescriptor = m_renderer.GetDescriptor<DebugSceneRenderer::DebugSceneDescriptor>();
	auto& frameDescriptor = m_renderer.GetFrameDescriptor();
	auto& scene = sceneDescriptor.scene;

	if (frameDescriptor.renderWidth == 0 || frameDescriptor.renderHeight == 0)
	{
		m_pickRequestPending = false;
		return;
	}

	m_actorPickingFramebuffer.Resize(frameDescriptor.renderWidth, frameDescriptor.renderHeight);

	m_actorPickingFramebuffer.Bind();

	// Only the pixel under the cursor is read back, so clip the whole pass to a
	// small region around it: the draw calls are still submitted, but almost all
	// fragments are scissored away before shading / depth write / bandwidth.
	constexpr uint32_t kPickRegionRadius = 24;
	const uint32_t frameWidth = frameDescriptor.renderWidth;
	const uint32_t frameHeight = frameDescriptor.renderHeight;
	const uint32_t regionLeft = m_pickRequestX > kPickRegionRadius
		? m_pickRequestX - kPickRegionRadius : 0u;
	const uint32_t regionBottom = m_pickRequestY > kPickRegionRadius
		? m_pickRequestY - kPickRegionRadius : 0u;
	const uint32_t regionRight = std::min(m_pickRequestX + kPickRegionRadius, frameWidth - 1);
	const uint32_t regionTop = std::min(m_pickRequestY + kPickRegionRadius, frameHeight - 1);
	m_renderer.SetScissor(
		regionLeft,
		regionBottom,
		regionRight - regionLeft + 1,
		regionTop - regionBottom + 1
	);

	// Capture the data MayTouchPickRegion() needs: the region in NDC (with a
	// small margin) and the projection scale for the screen radius estimate.
	const auto& camera = frameDescriptor.camera.value();
	const auto& projection = camera.GetProjectionMatrix();
	m_pickViewProjection = camera.GetViewProjectionMatrix();
	m_pickProjectionScaleX = projection.data[0];
	m_pickProjectionScaleY = projection.data[5];
	constexpr float kPickRegionMarginPixels = 2.0f;
	const float pixelToNdcX = 2.0f / static_cast<float>(frameWidth);
	const float pixelToNdcY = 2.0f / static_cast<float>(frameHeight);
	m_pickRegionNdcMinX =
		pixelToNdcX * (static_cast<float>(regionLeft) - kPickRegionMarginPixels) - 1.0f;
	m_pickRegionNdcMaxX =
		pixelToNdcX * (static_cast<float>(regionRight + 1) + kPickRegionMarginPixels) - 1.0f;
	m_pickRegionNdcMinY =
		pixelToNdcY * (static_cast<float>(regionBottom) - kPickRegionMarginPixels) - 1.0f;
	m_pickRegionNdcMaxY =
		pixelToNdcY * (static_cast<float>(regionTop + 1) + kPickRegionMarginPixels) - 1.0f;

	auto pso = m_renderer.CreatePipelineState();
	pso.scissorTest = true;

	m_renderer.Clear(true, true, true, Maths::FVector4::Zero, true);

	DrawPickableModels(pso, scene);
	//the following code has bugs and is temporarily disabled
	//DrawPickableCameras(pso, scene);
	//DrawPickableReflectionProbes(pso, scene);
	//DrawPickableLights(pso, scene);
	auto& gizmoInstance = MOON::ImRenderer::instance();
	gizmoInstance.drawMeshPick();

	// Restore a full target rectangle: the scissor test may still be enabled
	// until the next pipeline state is applied, and a full-screen box never clips.
	m_renderer.SetScissor(0, 0, frameWidth, frameHeight);

	// Clear depth, gizmos are rendered on top of everything else
	//m_renderer.Clear(false, true, false);

	//if (debugSceneDescriptor.selectedActor)
	//{
	//	auto& selectedActor = debugSceneDescriptor.selectedActor.value();

	//	DrawPickableGizmo(
	//		pso,
	//		selectedActor.transform.GetWorldPosition(),
	//		selectedActor.transform.GetWorldRotation(),
	//		debugSceneDescriptor.gizmoOperation
	//	);
	//}

	m_actorPickingFramebuffer.Unbind();

	// Serve a queued hover request: the picking target just finished, so the
	// pixel can be copied into the PBO ring without waiting for the GPU.
	IssuePickReadback();
	
	//the following code is for debug, it will display the picking framebuffer
	if (mPickOption.debug) {
		ImVec2 a = { 0,1 }, b = { 1,0 };
		ImVec2 size = ImVec2(frameDescriptor.renderWidth, frameDescriptor.renderHeight);
		auto resid=m_actorPickingFramebuffer.GetAttachment<::Rendering::HAL::GLTexture>(::Rendering::Settings::EFramebufferAttachment::COLOR,0);
		ImGui::Image(resid->GetID(), size, a, b);
	}
	if (auto output = frameDescriptor.outputMsaaBuffer)
	{
		output.value().Bind();
	}
}

void Editor::Rendering::PickingRenderPass::DrawPickableModels(
	::Rendering::Data::PipelineState p_pso,
	::Core::SceneSystem::Scene& p_scene
)
{
	const auto& filteredDrawables = m_renderer.GetDescriptor<::Core::Rendering::SceneRenderer::SceneFilteredDrawablesDescriptor>();
	auto drawPickableModels = [&](auto drawables) {
		for (auto& drawable : drawables)
		{			
			// Only the pick region is read back, and this pass submits one draw
			// per mesh, so drawables that cannot touch the region are skipped.
			if (!MayTouchPickRegion(drawable))
			{
				continue;
			}

			const auto& actor = drawable.GetDescriptor<::Core::Rendering::SceneRenderer::SceneDrawableDescriptor>().actor;
			if (actor.HasComponent("CBatchMeshTriangle")) {
			
				// Prioritize using the actual material state mask.
				//m_TopoShapePickingFallbackMaterial.SetDepthTest(true);
				auto stateMask = m_TopoShapePickingFallbackMaterial.GenerateStateMask();

				::Rendering::Entities::Drawable finalDrawable = drawable;
				finalDrawable.material = m_TopoShapePickingFallbackMaterial;
				finalDrawable.stateMask = stateMask;
				finalDrawable.stateMask.frontfaceCulling = false;
				finalDrawable.stateMask.backfaceCulling = false;
				m_renderer.DrawEntity(p_pso, finalDrawable);
			}
			else if (actor.HasComponent("CBatchMeshLine")) {
				// Prioritize using the actual material state mask.
				//m_TopoShapePickingFallbackMaterial.SetDepthTest(false);

				// Same as the main render: a line's depth can equal (or slightly
				// exceed, due to precision) its coplanar face, so use LEQUAL and
				// skip depth writes to make sure the picked ID is the line rather
				// than the face.
				p_pso.depthFunc = ::Rendering::Settings::EComparaisonAlgorithm::GREATER_EQUAL;

				auto stateMask = m_TopoShapePickingFallbackMaterial.GenerateStateMask();

				::Rendering::Entities::Drawable finalDrawable = drawable;
				finalDrawable.material = m_TopoShapePickingFallbackMaterial;
				
				finalDrawable.stateMask = stateMask;
				finalDrawable.stateMask.frontfaceCulling = false;
				finalDrawable.stateMask.backfaceCulling = false;
				finalDrawable.stateMask.depthWriting = false;
				m_renderer.DrawEntity(p_pso, finalDrawable);
			}
			else
			{
				const std::string pickingPassName = "PICKING_PASS";
				// If the material has picking pass, use it, otherwise use the picking fallback material
				auto& targetMaterial =
					(drawable.material && drawable.material->IsValid() && drawable.material->HasPass(pickingPassName)) ?
					drawable.material.value() :
					m_actorPickingFallbackMaterial;

				PreparePickingMaterial(actor, targetMaterial);
				// Prioritize using the actual material state mask.
				auto stateMask =
					drawable.material && drawable.material->IsValid() ?
					drawable.material->GenerateStateMask() :
					targetMaterial.GenerateStateMask();

				::Rendering::Entities::Drawable finalDrawable = drawable;
				finalDrawable.material = targetMaterial;
				finalDrawable.stateMask = stateMask;
				finalDrawable.stateMask.frontfaceCulling = false;
				finalDrawable.stateMask.backfaceCulling = false;
				finalDrawable.pass = pickingPassName;
				m_renderer.DrawEntity(p_pso, finalDrawable);
			}

		}
		};

	drawPickableModels(filteredDrawables.opaques | std::views::values);
	drawPickableModels(filteredDrawables.lines | std::views::values);
	drawPickableModels(filteredDrawables.transparents | std::views::values);
	drawPickableModels(filteredDrawables.ui | std::views::values);
}

bool Editor::Rendering::PickingRenderPass::MayTouchPickRegion(
	const ::Rendering::Entities::Drawable& p_drawable
) const
{
	const auto& desc =
		p_drawable.GetDescriptor<::Core::Rendering::SceneRenderer::SceneDrawableDescriptor>();
	if (!desc.bounds.has_value())
	{
		// Without bounds the drawable cannot be rejected safely.
		return true;
	}

	const auto& sphere = desc.bounds.value();
	const Maths::FMatrix4& model = desc.actor.transform.GetWorldMatrix();
	const Maths::FVector4 worldCenter = model * Maths::FVector4(
		sphere.position.x,
		sphere.position.y,
		sphere.position.z,
		1.0f
	);
	const Maths::FVector4 clip = m_pickViewProjection * worldCenter;
	if (clip.w <= 1e-5f)
	{
		// Behind / straddling the near plane: keep it (conservative).
		return true;
	}

	const float invW = 1.0f / clip.w;
	const float ndcX = clip.x * invW;
	const float ndcY = clip.y * invW;

	// Conservative screen-space radius: a sphere of radius r at clip w covers
	// roughly r * projectionScale / w in NDC. Treating the circle as a box (and
	// keeping a margin) makes the rejection safe for the picking pass.
	constexpr float kRadiusBias = 1.25f;
	const float radiusX = std::abs(m_pickProjectionScaleX) * sphere.radius * invW * kRadiusBias;
	const float radiusY = std::abs(m_pickProjectionScaleY) * sphere.radius * invW * kRadiusBias;

	return !(
		ndcX + radiusX < m_pickRegionNdcMinX
		|| ndcX - radiusX > m_pickRegionNdcMaxX
		|| ndcY + radiusY < m_pickRegionNdcMinY
		|| ndcY - radiusY > m_pickRegionNdcMaxY
		);
}
Editor::Rendering::PickPassOption& Editor::Rendering::PickingRenderPass::GetPickPassOption()
{
	return mPickOption;
}
