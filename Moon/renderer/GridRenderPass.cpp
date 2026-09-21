#include "EditorResources.h"
#include "DebugModelRenderFeature.h"
#include "GridRenderPass.h"
#include "Core/Global/ServiceLocator.h"
#include "Settings/DebugSetting.h"
#include "Core/Rendering/SceneRenderer.h"
#include "Core/Rendering/EngineBufferRenderFeature.h"
#include <Rendering/Features/DebugShapeRenderFeature.h>
#include <Rendering/HAL/Profiling.h>
#include "Qtimgui/imgui/imgui.h"



Editor::Rendering::GridRenderPass::GridRenderPass(::Rendering::Core::CompositeRenderer& p_renderer) :

	::Rendering::Core::ARenderPass(p_renderer)
{
	/* Grid Material */
	m_gridMaterial.SetShader(GetShaderService[":Shaders/Grid.ovfx"]);
	m_gridMaterial.SetBlendable(true);
	m_gridMaterial.SetBackfaceCulling(true);
	m_gridMaterial.SetDepthWriting(false);
	m_gridMaterial.SetDepthTest(true);

	m_ShadowMaterial.SetShader(GetShaderService[":Shaders\\MirrorPlaneShadow.ovfx"]);
	m_ShadowMaterial.SetBackfaceCulling(true);
	m_ShadowMaterial.SetDepthTest(false);
	m_ShadowMaterial.SetDepthWriting(false);


	m_BlurMaterial.SetShader(GetShaderService[":Shaders\\PostProcess\\blur.ovfx"]);
	//m_BlurMaterial.AddFeature("k15x15");



	::Rendering::Settings::TextureDesc colorDesc{
		.width = 1,
		.height = 1,
		.minFilter = ::Rendering::Settings::ETextureFilteringMode::LINEAR,
		.magFilter = ::Rendering::Settings::ETextureFilteringMode::LINEAR,
		.horizontalWrap = ::Rendering::Settings::ETextureWrapMode::CLAMP_TO_BORDER,
		.verticalWrap = ::Rendering::Settings::ETextureWrapMode::CLAMP_TO_BORDER,
		.internalFormat = ::Rendering::Settings::EInternalFormat::RGBA32F,
		.useMipMaps = false,
		.mutableDesc = ::Rendering::Settings::MutableTextureDesc{
			.format = ::Rendering::Settings::EFormat::RGBA,
			.type = ::Rendering::Settings::EPixelDataType::FLOAT
		}
	};
	::Rendering::Settings::TextureDesc shadowDesc{
	.width = 1,
	.height = 1,
	.minFilter = ::Rendering::Settings::ETextureFilteringMode::LINEAR,
	.magFilter = ::Rendering::Settings::ETextureFilteringMode::LINEAR,
	.horizontalWrap = ::Rendering::Settings::ETextureWrapMode::CLAMP_TO_BORDER,
	.verticalWrap = ::Rendering::Settings::ETextureWrapMode::CLAMP_TO_BORDER,
	.internalFormat = ::Rendering::Settings::EInternalFormat::RGBA32F,
	.useMipMaps = false,
	.mutableDesc = ::Rendering::Settings::MutableTextureDesc{
		.format = ::Rendering::Settings::EFormat::RGBA,
		.type = ::Rendering::Settings::EPixelDataType::FLOAT
	}
	};
	::Rendering::Settings::TextureDesc depthDesc{
		.width = 1,
		.height = 1,
		.minFilter = ::Rendering::Settings::ETextureFilteringMode::NEAREST,
		.magFilter = ::Rendering::Settings::ETextureFilteringMode::NEAREST,
		.horizontalWrap = ::Rendering::Settings::ETextureWrapMode::REPEAT,
		.verticalWrap = ::Rendering::Settings::ETextureWrapMode::REPEAT,
		.internalFormat = ::Rendering::Settings::EInternalFormat::DEPTH_COMPONENT32,
		.useMipMaps = false,
		.mutableDesc = ::Rendering::Settings::MutableTextureDesc{
			.format = ::Rendering::Settings::EFormat::DEPTH_COMPONENT,
			.type = ::Rendering::Settings::EPixelDataType::FLOAT
		}
	};
	
	auto renderTexture = std::make_shared<::Rendering::HAL::Texture>(
		::Rendering::Settings::ETextureType::TEXTURE_2D, "color");
	renderTexture->Allocate(colorDesc);
	auto depthTexture = std::make_shared<::Rendering::HAL::Texture>(
		::Rendering::Settings::ETextureType::TEXTURE_2D, "depth");
	depthTexture->Allocate(depthDesc);
	depthTexture->SetBorderColor(Maths::FVector4::One);
	m_mirroFbo.Attach<::Rendering::HAL::Texture>(renderTexture, ::Rendering::Settings::EFramebufferAttachment::COLOR);
	m_mirroFbo.Attach<::Rendering::HAL::Texture>(depthTexture, ::Rendering::Settings::EFramebufferAttachment::DEPTH);
	for (auto& mirroShadowFbo : m_mirroShadowFbo.GetFramebuffers()) {
		auto shadowTexture = std::make_shared<::Rendering::HAL::Texture>(::Rendering::Settings::ETextureType::TEXTURE_2D, "color");
		shadowTexture->Allocate(shadowDesc);
		mirroShadowFbo.Attach<::Rendering::HAL::Texture>(shadowTexture, ::Rendering::Settings::EFramebufferAttachment::COLOR);
	}

}

void Editor::Rendering::GridRenderPass::Draw(::Rendering::Data::PipelineState p_pso)
{
	ZoneScoped;
	TracyGpuZone("GridRenderPass");
	
	assert(m_renderer.HasDescriptor<GridDescriptor>()&&"Cannot find GridDescriptor attached to this renderer");
	assert(m_renderer.HasFeature<Editor::Rendering::DebugModelRenderFeature>()&&"Cannot find DebugModelRenderFeature attached to this renderer");
	
	auto& frameDesc = m_renderer.GetFrameDescriptor();
	auto& gridDescriptor = m_renderer.GetDescriptor<GridDescriptor>();
	auto plane=Maths::FVector4(gridDescriptor.mirrorPlaneNormal,-gridDescriptor.mirrorPlaneNormal.Dot(gridDescriptor.mirrorPlaneCenter));
	auto& engineBufferRenderFeature =m_renderer.GetFeature<::Core::Rendering::EngineBufferRenderFeature>();
	Maths::FMatrix4 mirrorMat=Maths::FMatrix4::MirrorPlane(plane.x, plane.y, plane.z, plane.w);
	Maths::FMatrix4 viewMat= frameDesc.camera->GetViewMatrix();
	viewMat =  viewMat*mirrorMat ;
	engineBufferRenderFeature.SetViewMatrix(viewMat);
	engineBufferRenderFeature.SetViewPos(mirrorMat.MulPoint(frameDesc.camera->GetPosition()));
	engineBufferRenderFeature.SetMirrorPlane(mirrorMat);

	/*
	Step 1. 
	Render the models to the mirror framebuffer,from the mirror camera,and generate shadow.
	*/
	const auto& filteredDrawables = m_renderer.GetDescriptor<::Core::Rendering::SceneRenderer::SceneFilteredDrawablesDescriptor>();
	auto drawMirroModels = [&](auto drawables,bool transparent) {
		for (auto& drawable : drawables)
		{
			auto drawableCopy = drawable;
			if (transparent) {
				drawableCopy.pass = "";
			}
			m_renderer.DrawEntity(p_pso, drawableCopy);
		}
	};
	auto drawMirroShadowModels = [&](auto drawables) {
		for (auto& drawable : drawables)
		{
			auto drawableCopy = drawable;
			drawableCopy.material = m_ShadowMaterial;
			m_renderer.DrawEntity(p_pso, drawableCopy);
		}
	};
	m_mirroFbo.Bind();
	m_renderer.Clear(true, true, false, Maths::FVector4(1.0, 1.0, 1.0 ,0.2));
	drawMirroModels(filteredDrawables.opaques | std::views::values,false);
	drawMirroModels(filteredDrawables.lines | std::views::values, false);
	drawMirroModels(filteredDrawables.transparents | std::views::values,true);
	m_mirroFbo.Unbind();

	m_mirroShadowFbo[0].Bind();
	m_renderer.Clear(true, true, false, Maths::FVector4(0, 0, 0, 1));
	m_ShadowMaterial.SetProperty("uMirroPlane", plane);
	drawMirroShadowModels(filteredDrawables.opaques | std::views::values);

	for (int i = 0; i < 6; i++) {
		m_BlurMaterial.SetProperty("horizontal",i%2?true:false);
		m_renderer.Blit(p_pso, m_mirroShadowFbo[0], m_mirroShadowFbo[1], m_BlurMaterial);

		++m_mirroShadowFbo;
	}
	
	m_mirroShadowFbo[0].Unbind();

	/*
	Step 2.
	Blend the mirror framebuffer to the main framebuffer with grid shader, from the main camera
	*/
	engineBufferRenderFeature.SetCamera(frameDesc.camera.value());
	auto& msaaframebuffer = frameDesc.outputMsaaBuffer.value();
	msaaframebuffer.Bind();
	auto pso = m_renderer.CreatePipelineState();
	// Size the mirror plane so it stays visible in large scenes: cover the
	// scene footprint and the camera's view at the plane instead of a fixed
	// 1000x1000 area, which becomes a tiny speck when the camera is far away.
	float gridSize = 500.0f;
	if (m_renderer.HasDescriptor<::Core::Rendering::SceneRenderer::SceneDescriptor>()) {
		const auto& sceneDescriptor = m_renderer.GetDescriptor<::Core::Rendering::SceneRenderer::SceneDescriptor>();
		if (auto* bvh = sceneDescriptor.scene.GetBvh()) {
			const auto& b = bvh->m_bounds;
			const float sceneExtent = Maths::FVector3::Length(b.pmax - b.pmin);
			gridSize = std::max(gridSize, sceneExtent * 0.6f);
		}
	}
	const float camDist = Maths::FVector3::Length(
		frameDesc.camera->GetPosition() - gridDescriptor.mirrorPlaneCenter);
	gridSize = std::max(gridSize, camDist * 0.8f);
	Maths::FMatrix4 model = Maths::FMatrix4::ComputeTransformFromYAxisAndOrigin(-gridDescriptor.mirrorPlaneNormal, gridDescriptor.mirrorPlaneCenter, { gridSize * 2.0f, 1.f, gridSize * 2.0f });
	//Maths::FMatrix4::Translation({ gridDescriptor.viewPosition.x, 0.0f, gridDescriptor.viewPosition.z }) *
	//Maths::FMatrix4::Scaling({ gridSize * 2.0f, 1.f, gridSize * 2.0f });

	model = Maths::FMatrix4::Transpose(model);
	const auto& color = m_mirroFbo.GetAttachment<::Rendering::HAL::GLTexture>(::Rendering::Settings::EFramebufferAttachment::COLOR, 0);
	const auto& shadow = m_mirroShadowFbo[0].GetAttachment<::Rendering::HAL::GLTexture>(::Rendering::Settings::EFramebufferAttachment::COLOR, 0);
	m_gridMaterial.SetProperty("u_Color", gridDescriptor.gridColor*0.7);
	m_gridMaterial.SetProperty("u_PlaneTransform", model);
	m_gridMaterial.SetProperty("u_MirrorTex", &color.value());
	m_gridMaterial.SetProperty("u_MirrorShadowTex", &shadow.value());
	m_renderer.GetFeature<DebugModelRenderFeature>()
		.DrawModelWithSingleMaterial(pso, *GetModelService[":Models/Plane.fbx"], m_gridMaterial, model);

	//debug
	//if (false) {
	//	ImVec2 a = { 0,1 }, b = { 1,0 };
	//	ImVec2 size = ImVec2(frameDesc.renderWidth, frameDesc.renderHeight);
	//	auto resid = m_mirroShadowFbo.GetAttachment<::Rendering::HAL::GLTexture>(::Rendering::Settings::EFramebufferAttachment::COLOR, 0);
	//	ImGui::Image(resid->GetID(), size, a, b);
	//}
}

void Editor::Rendering::GridRenderPass::ResizeRenderer(int width, int height)
{
	m_mirroFbo.Resize(width,height);
	m_mirroShadowFbo[0].Resize(width, height);
	m_mirroShadowFbo[1].Resize(width, height);
}
