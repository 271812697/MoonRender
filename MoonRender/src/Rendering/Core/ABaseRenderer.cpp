#include <functional>
#include <tracy/Tracy.hpp>
#include <Rendering/Core/ABaseRenderer.h>
#include <Rendering/HAL/TextureHandle.h>
#include <Rendering/Resources/Loaders/TextureLoader.h>
#include <Rendering/Resources/Loaders/ShaderLoader.h>
std::atomic_bool Rendering::Core::ABaseRenderer::s_isDrawing{ false };

namespace
{
	const Rendering::Entities::Camera kDefaultCamera(Maths::FTransform());

	std::vector<Rendering::Geometry::Vertex> kUnitQuadVertices = {
		{ {-1.0f, -1.0f, 0.0f}, {0.0f, 0.0f} }, // Bottom-left
		{ { 1.0f, -1.0f, 0.0f}, {1.0f, 0.0f} }, // Bottom-right
		{ { 1.0f,  1.0f, 0.0f}, {1.0f, 1.0f} }, // Top-right
		{ {-1.0f,  1.0f, 0.0f}, {0.0f, 1.0f} }  // Top-left
	};
	std::vector<uint32_t> kUnitQuadIndices ={
		0, 1, 2, // First triangle
		0, 2, 3  // Second triangle
		};
	std::vector<Rendering::Geometry::Vertex> kUnitCubeVertices = {
		// back face
		{{  -1.0f, -1.0f, -1.0f}, {0.0f, 0.0f}, { 0.0f,  0.0f, -1.0f}}, // bottom-left
		{{ 1.0f,  1.0f, -1.0f}, {1.0f, 1.0f},  {0.0f,  0.0f, -1.0f}}, // top-right
		{{  1.0f, -1.0f, -1.0f},{ 1.0f, 0.0f},  {0.0f,  0.0f, -1.0f}}, // bottom-right         
		{{  1.0f,  1.0f, -1.0f}, {1.0f, 1.0f},  {0.0f,  0.0f, -1.0f}}, // top-right
		{{ -1.0f, -1.0f, -1.0f}, {0.0f, 0.0f},  {0.0f,  0.0f, -1.0f}}, // bottom-left
		{{ -1.0f,  1.0f, -1.0f}, {0.0f, 1.0f},  {0.0f,  0.0f, -1.0f}}, // top-left
		// front face
		{{ -1.0f, -1.0f,  1.0f}, {0.0f, 0.0f},  {0.0f,  0.0f,  1.0f}}, // bottom-left
		{{  1.0f, -1.0f,  1.0f}, {1.0f, 0.0f},  {0.0f,  0.0f,  1.0f}}, // bottom-right
		{{  1.0f,  1.0f,  1.0f}, {1.0f, 1.0f},  {0.0f,  0.0f,  1.0f}}, // top-right
		{{  1.0f,  1.0f,  1.0f}, {1.0f, 1.0f},  {0.0f,  0.0f,  1.0f}}, // top-right
		{{ -1.0f,  1.0f,  1.0f}, {0.0f, 1.0f},  {0.0f,  0.0f,  1.0f}}, // top-left
		{{ -1.0f, -1.0f,  1.0f}, {0.0f, 0.0f},  {0.0f,  0.0f,  1.0f}}, // bottom-left
		// left face
		{{ -1.0f,  1.0f,  1.0f}, {1.0f, 0.0f}, {-1.0f,  0.0f,  0.0f}}, // top-right
		{{ -1.0f,  1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f,  0.0f,  0.0f}}, // top-left
		{{ -1.0f, -1.0f, -1.0f}, {0.0f, 1.0f}, {-1.0f,  0.0f,  0.0f}}, // bottom-left
		{{ -1.0f, -1.0f, -1.0f}, {0.0f, 1.0f}, {-1.0f,  0.0f,  0.0f}}, // bottom-left
		{{ -1.0f, -1.0f,  1.0f}, {0.0f, 0.0f}, {-1.0f,  0.0f,  0.0f}}, // bottom-right
		{{ -1.0f,  1.0f,  1.0f}, {1.0f, 0.0f}, {-1.0f,  0.0f,  0.0f}}, // top-right
		// right face
		{{  1.0f,  1.0f,  1.0f}, {1.0f, 0.0f},  {1.0f,  0.0f,  0.0f}}, // top-left
		{{  1.0f, -1.0f, -1.0f}, {0.0f, 1.0f},  {1.0f,  0.0f,  0.0f}}, // bottom-right
		{{  1.0f,  1.0f, -1.0f}, {1.0f, 1.0f},  {1.0f,  0.0f,  0.0f}}, // top-right         
		{{  1.0f, -1.0f, -1.0f}, {0.0f, 1.0f},  {1.0f,  0.0f,  0.0f}}, // bottom-right
		{{  1.0f,  1.0f,  1.0f}, {1.0f, 0.0f},  {1.0f,  0.0f,  0.0f}}, // top-left
		{{  1.0f, -1.0f,  1.0f}, {0.0f, 0.0f},  {1.0f,  0.0f,  0.0f}}, // bottom-left     
		// bottom face
	   {{  -1.0f, -1.0f, -1.0f}, {0.0f, 1.0f},  {0.0f, -1.0f,  0.0f}}, // top-right
	   {{   1.0f, -1.0f, -1.0f}, {1.0f, 1.0f},  {0.0f, -1.0f,  0.0f}}, // top-left
	   {{   1.0f, -1.0f,  1.0f}, {1.0f, 0.0f},  {0.0f, -1.0f,  0.0f}}, // bottom-left
	   {{   1.0f, -1.0f,  1.0f}, {1.0f, 0.0f},  {0.0f, -1.0f,  0.0f}}, // bottom-left
	   {{  -1.0f, -1.0f,  1.0f}, {0.0f, 0.0f},  {0.0f, -1.0f,  0.0f}}, // bottom-right
	   {{  -1.0f, -1.0f, -1.0f}, {0.0f, 1.0f},  {0.0f, -1.0f,  0.0f}}, // top-right
	   // top face
	  {{  -1.0f,  1.0f, -1.0f}, {0.0f, 1.0f},  {0.0f,  1.0f,  0.0f}}, // top-left
	  {{   1.0f,  1.0f , 1.0f}, {1.0f, 0.0f},  {0.0f,  1.0f,  0.0f}}, // bottom-right
	  {{   1.0f,  1.0f, -1.0f}, {1.0f, 1.0f},  {0.0f,  1.0f,  0.0f}}, // top-right     
	  {{   1.0f,  1.0f,  1.0f}, {1.0f, 0.0f},  {0.0f,  1.0f,  0.0f}}, // bottom-right
	  {{  -1.0f,  1.0f, -1.0f}, {0.0f, 1.0f},  {0.0f,  1.0f,  0.0f}}, // top-left
	  {{  -1.0f,  1.0f,  1.0f}, {0.0f, 0.0f},  {0.0f,  1.0f,  0.0f}}  // bottom-left    
    
	};
	std::vector<uint32_t> kUnitCubeIndices = {
	0, 1, 2, 3, 4, 5 ,
	6, 7, 8, 9, 10, 11,
	12, 13,14, 15, 16, 17,
	18, 19, 20, 21, 22, 23,
	24, 25, 26, 27, 28, 29,
	30, 31, 32, 33, 34, 35
	
	};
	constexpr auto kWhitePixel = std::to_array<uint8_t>({ 255, 255, 255, 255 });
	constexpr auto kBlackPixel = std::to_array<uint8_t, 6 * 4>({ 0 });
}

Rendering::Core::ABaseRenderer::ABaseRenderer(Context::Driver& p_driver) :
	m_driver(p_driver),
	m_isDrawing(false),
	m_emptyTexture2D{ Settings::ETextureType::TEXTURE_2D },
	m_emptyTextureCube{ Settings::ETextureType::TEXTURE_CUBE },
	m_unitQuad(kUnitQuadVertices, kUnitQuadIndices),
	m_unitCube(kUnitCubeVertices, kUnitCubeIndices)
{
	const auto kEmptyTextureDesc = Settings::TextureDesc{
		.width = 1,
		.height = 1,
		.minFilter = Settings::ETextureFilteringMode::NEAREST,
		.magFilter = Settings::ETextureFilteringMode::NEAREST,
		.horizontalWrap = Settings::ETextureWrapMode::REPEAT,
		.verticalWrap = Settings::ETextureWrapMode::REPEAT,
		.internalFormat = Settings::EInternalFormat::RGBA8,
		.useMipMaps = false
	};

	m_emptyTexture2D.Allocate(kEmptyTextureDesc);
	m_emptyTexture2D.Upload(kWhitePixel.data(), Settings::EFormat::RGBA, Settings::EPixelDataType::UNSIGNED_BYTE);

	m_emptyTextureCube.Allocate(kEmptyTextureDesc);
	m_emptyTextureCube.Upload(kBlackPixel.data(), Settings::EFormat::RGBA, Settings::EPixelDataType::UNSIGNED_BYTE);
	std::string v = R"(
#version 450 core

layout(location = 0) in vec2 geo_Pos;
layout(location = 1) in vec2 geo_TexCoords;

out vec2 TexCoords;

void main()
{
    TexCoords = geo_TexCoords;
    gl_Position = vec4(geo_Pos, 0.0, 1.0);
}
)";
	std::string f = R"(
#version 450 core

in vec2 TexCoords;
out vec4 FRAGMENT_COLOR;

uniform sampler2D _InputTexture;

void main()
{
    FRAGMENT_COLOR = texture(_InputTexture, TexCoords);
}

)";
	//not free the m_presentShader shader?
	m_presentShader = Rendering::Resources::Loaders::ShaderLoader::CreateFromSource(v, f);
	m_presentMaterial.SetShader(m_presentShader);
}

void Rendering::Core::ABaseRenderer::BeginFrame(const Data::FrameDescriptor& p_frameDescriptor)
{
	ZoneScoped;

	m_previousMaterialSignature.reset();
	m_frameDescriptor = p_frameDescriptor;

	if (p_frameDescriptor.outputMsaaBuffer)
	{
		p_frameDescriptor.outputMsaaBuffer.value().Bind();
	}

	m_basePipelineState = m_driver.CreatePipelineState();
	SetViewport(0, 0, p_frameDescriptor.renderWidth, p_frameDescriptor.renderHeight);

	Clear(
		p_frameDescriptor.camera->GetClearColorBuffer(),
		p_frameDescriptor.camera->GetClearDepthBuffer(),
		p_frameDescriptor.camera->GetClearStencilBuffer(),
		{ p_frameDescriptor.camera.value().GetClearColor(), 1.0f }
	);

	p_frameDescriptor.camera->CacheMatrices(p_frameDescriptor.renderWidth, p_frameDescriptor.renderHeight);

	m_isDrawing = true;
	s_isDrawing.store(true);
}

void Rendering::Core::ABaseRenderer::EndFrame()
{
	ZoneScoped;

	if (m_frameDescriptor.outputMsaaBuffer)
	{
		m_frameDescriptor.outputMsaaBuffer.value().Unbind();
	}

	m_isDrawing = false;
	s_isDrawing.store(false);
}

const Rendering::Data::FrameDescriptor& Rendering::Core::ABaseRenderer::GetFrameDescriptor() const
{
	return m_frameDescriptor;
}

Rendering::Data::PipelineState Rendering::Core::ABaseRenderer::CreatePipelineState() const
{
	return m_basePipelineState;
}

bool Rendering::Core::ABaseRenderer::IsDrawing() const
{
	return m_isDrawing;
}

void Rendering::Core::ABaseRenderer::SetViewport(uint32_t p_x, uint32_t p_y, uint32_t p_width, uint32_t p_height)
{
	m_driver.SetViewport(p_x, p_y, p_width, p_height);
}

void Rendering::Core::ABaseRenderer::SetScissor(uint32_t p_x, uint32_t p_y, uint32_t p_width, uint32_t p_height)
{
	m_driver.SetScissor(p_x, p_y, p_width, p_height);
}

void Rendering::Core::ABaseRenderer::Clear(
	bool p_colorBuffer,
	bool p_depthBuffer,
	bool p_stencilBuffer,
	const Maths::FVector4& p_color,
	bool p_scissor
)
{
	ZoneScoped;
	m_driver.Clear(p_colorBuffer, p_depthBuffer, p_stencilBuffer, p_color, p_scissor);
}

void Rendering::Core::ABaseRenderer::Blit(
	Rendering::Data::PipelineState p_pso,
	Rendering::HAL::Framebuffer& p_src,
	Rendering::HAL::Framebuffer& p_dst,
	Rendering::Data::Material& p_material,
	Rendering::Settings::EBlitFlags p_flags
)
{
	ZoneScoped;

	const auto [srcWidth, srcHeight] = p_src.GetSize();

	if (Rendering::Settings::IsFlagSet(Rendering::Settings::EBlitFlags::RESIZE_DST_TO_MATCH_SRC, p_flags))
	{
		p_dst.Resize(srcWidth, srcHeight);
	}

	if (Rendering::Settings::IsFlagSet(Rendering::Settings::EBlitFlags::FILL_INPUT_TEXTURE, p_flags))
	{
		const auto colorTex = p_src.GetAttachment<HAL::Texture>(Settings::EFramebufferAttachment::COLOR);
		
		p_material.SetProperty("_InputTexture", &colorTex.value());
	}

	Rendering::Entities::Drawable blit;
	blit.mesh = m_unitQuad;
	blit.material = p_material;

	if (Rendering::Settings::IsFlagSet(Rendering::Settings::EBlitFlags::USE_MATERIAL_STATE_MASK, p_flags))
	{
		blit.stateMask = p_material.GenerateStateMask();
	}
	else
	{
		blit.stateMask.depthWriting = false;
		blit.stateMask.colorWriting = true;
		blit.stateMask.blendable = false;
		blit.stateMask.frontfaceCulling = false;
		blit.stateMask.backfaceCulling = false;
		blit.stateMask.depthTest = false;
	}

	p_dst.Bind();

	if (Rendering::Settings::IsFlagSet(Rendering::Settings::EBlitFlags::UPDATE_VIEWPORT_SIZE, p_flags))
	{
		const auto [dstWidth, dstHeight] = p_dst.GetSize();
		SetViewport(0, 0, dstWidth, dstHeight);
	}

	DrawEntity(p_pso, blit);
	p_dst.Unbind();
}

void Rendering::Core::ABaseRenderer::Present(Rendering::HAL::Framebuffer& p_src)
{
	Present(p_src, m_presentMaterial);
}
void Rendering::Core::ABaseRenderer::Present(Rendering::HAL::Framebuffer& p_src,Rendering::Data::Material& mat)
{
	const auto colorTex = p_src.GetAttachment<HAL::Texture>(Settings::EFramebufferAttachment::COLOR);
	Present(colorTex.value(),mat);
}
void Rendering::Core::ABaseRenderer::Present(Rendering::HAL::Texture& p_src, Rendering::Data::Material& mat)
{
	ZoneScoped;
	mat.SetProperty("_InputTexture", &p_src);
	Present(mat);
}
void Rendering::Core::ABaseRenderer::Present(Rendering::Data::Material& mat)
{
	ZoneScoped;


	
	Rendering::Entities::Drawable blit;
	blit.mesh = m_unitQuad;
	blit.material = mat;
	blit.stateMask.depthWriting = false;
	blit.stateMask.colorWriting = true;
	blit.stateMask.blendable = false;

	blit.stateMask.frontfaceCulling = false;
	blit.stateMask.backfaceCulling = false;
	blit.stateMask.depthTest = false;
	auto pso = CreatePipelineState();

	auto material = blit.material;
	auto mesh = blit.mesh;

	const auto gpuInstances = material.value().GetGPUInstances();

	if (mesh && material && material->IsValid() && gpuInstances > 0)
	{
		pso.depthWriting = blit.stateMask.depthWriting;
		pso.colorWriting.mask = blit.stateMask.colorWriting ? 0xFF : 0x00;
		pso.blending = blit.stateMask.blendable;
		//p_pso.userInterface = blit.stateMask.userInterface;
		pso.culling = blit.stateMask.frontfaceCulling || blit.stateMask.backfaceCulling;
		pso.depthTest = blit.stateMask.depthTest;

		if (pso.culling)
		{
			if (blit.stateMask.backfaceCulling && blit.stateMask.frontfaceCulling)
			{
				pso.cullFace = Settings::ECullFace::FRONT_AND_BACK;
			}
			else
			{
				pso.cullFace =
					blit.stateMask.backfaceCulling ?
					Settings::ECullFace::BACK :
					Settings::ECullFace::FRONT;
			}
		}

		material->Bind(nullptr);
		// Bind() only selects the program now; callers that do not go through
		// DrawEntity have to push the uniforms themselves, otherwise the
		// samplers keep their default value and the blit samples garbage.
		material->UploadProperties(true, true);
		m_driver.Draw(pso, mesh.value(), blit.primitiveMode, gpuInstances);
		material->Unbind();
	}
}
void Rendering::Core::ABaseRenderer::Present(Rendering::HAL::Texture& p_src)
{	
	Present(p_src, m_presentMaterial);
}
bool Rendering::Core::ABaseRenderer::IsDrawable(const Entities::Drawable& p_drawable) const
{
	return
		p_drawable.mesh &&
		p_drawable.material &&
		p_drawable.material->IsValid() &&
		p_drawable.material->SupportsProjectionMode(m_frameDescriptor.camera->GetProjectionMode()) &&
		p_drawable.material->GetGPUInstances() > 0;
}

void Rendering::Core::ABaseRenderer::DrawEntity(
	Rendering::Data::PipelineState p_pso,
	const Entities::Drawable& p_drawable
)
{
	ZoneScoped;
	p_pso.depthWriting = p_drawable.stateMask.depthWriting;
	p_pso.colorWriting.mask = p_drawable.stateMask.colorWriting ? 0xFF : 0x00;
	p_pso.blending = p_drawable.stateMask.blendable;

	// The state mask carries the material's blend mode: turn it into the factors the driver
	// applies. Anything that is not opaque falls back to the standard alpha blend.
	{
		using Settings::EBlendMode;
		using Settings::EBlendingFactor;
		using Settings::EBlendingEquation;

		p_pso.blendingSrcFactor = EBlendingFactor::SRC_ALPHA;
		p_pso.blendingDestFactor = EBlendingFactor::ONE_MINUS_SRC_ALPHA;
		p_pso.blendingEquation = EBlendingEquation::FUNC_ADD;

		switch (p_drawable.stateMask.blendMode)
		{
		case EBlendMode::BLEND_ADD:
			p_pso.blendingDestFactor = EBlendingFactor::ONE;
			break;
		case EBlendMode::BLEND_MULTIPLY:
			p_pso.blendingSrcFactor = EBlendingFactor::DST_COLOR;
			p_pso.blendingDestFactor = EBlendingFactor::ZERO;
			break;
		case EBlendMode::BLEND_SCREEN:
			p_pso.blendingSrcFactor = EBlendingFactor::ONE;
			p_pso.blendingDestFactor = EBlendingFactor::ONE_MINUS_SRC_COLOR;
			break;
		case EBlendMode::BLEND_FADE:
		case EBlendMode::BLEND_TRANSPARENT:
		case EBlendMode::BLEND_OPAQUE:
		default:
			break;
		}
	}
	p_pso.culling = p_drawable.stateMask.frontfaceCulling || p_drawable.stateMask.backfaceCulling;
	p_pso.depthTest = p_drawable.stateMask.depthTest;
	p_pso.lineWidth = p_drawable.stateMask.lineWidth;
	p_pso.polygonOffsetFill = p_drawable.stateMask.polygonOffsetFill;
	
	if (p_pso.culling)
	{
		if (p_drawable.stateMask.backfaceCulling && p_drawable.stateMask.frontfaceCulling)
		{
			p_pso.cullFace = Settings::ECullFace::FRONT_AND_BACK;
		}
		else
		{
			p_pso.cullFace =
				p_drawable.stateMask.backfaceCulling ?
				Settings::ECullFace::BACK :
				Settings::ECullFace::FRONT;
		}
	}

	const auto signature=p_drawable.material->Bind(
		&m_emptyTexture2D,
		&m_emptyTextureCube,
		p_drawable.pass,
		p_drawable.featureSetOverride.has_value() ?
		Tools::Utils::OptRef<const Data::FeatureSet>(p_drawable.featureSetOverride.value()) :
		std::nullopt,
		m_previousMaterialSignature
	);
	const bool uploadStableProperties = !m_previousMaterialSignature.has_value() || signature.stablePropertySignature != m_previousMaterialSignature->stablePropertySignature;
	const bool uploadSingleUseProperties = !m_previousMaterialSignature.has_value() || signature.singleUsePropertySignature != m_previousMaterialSignature->singleUsePropertySignature;

	if (uploadStableProperties || uploadSingleUseProperties)
	{
		p_drawable.material->UploadProperties(
			uploadStableProperties,
			uploadSingleUseProperties,
			&m_emptyTexture2D,
			&m_emptyTextureCube,
			m_brdfLut
		);
	}

	m_previousMaterialSignature = signature;

	// The DFG LUT is owned by the view, not by the material: the sky pass bakes it after the
	// material's first upload has already run, and uploads only happen when the material's
	// signature changes. Without this per-draw bind the material keeps the white empty 2D that
	// was bound on the first frame (DFG=(1,1), E~=1, diffuse multiplied by zero and the
	// dielectric looking like a full mirror).
	p_drawable.material->BindGlobalTexture("_BRDFLut", m_brdfLut);

	m_driver.Draw(
		p_pso,
		p_drawable.mesh.value(),
		p_drawable.subIndexBufferIndex,
		p_drawable.primitiveMode,
		p_drawable.material->GetGPUInstances()
	);

	p_drawable.material->Unbind();
}
