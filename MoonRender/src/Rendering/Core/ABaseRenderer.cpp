#include <functional>
#include <tracy/Tracy.hpp>
#include <Rendering/Core/ABaseRenderer.h>

#include <cstdio>
#include <cstdlib>
#include <Rendering/Backend/DrawMaterialRequest.h>
#include <Rendering/Data/DescriptorSet.h>
#include <Rendering/Data/EngineDescriptorSets.h>
#include <Rendering/Data/ProgramDescriptorBindings.h>
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
	/** The neutral tangent-space normal: a normal map decodes (0.5, 0.5, 1.0) to (0, 0, 1),
	 * i.e. "no perturbation". 128/255 is 0.502, close enough that the decoded vector is
	 * (0.004, 0.004, 1.0) and normalizes to (0, 0, 1). */
	constexpr auto kFlatNormalPixel = std::to_array<uint8_t>({ 128, 128, 255, 255 });
}

namespace
{
	/** MOON_FRAME_TRACE prints BeginFrame's steps: the bring-up tool for finding the one that blocks. */
	void TraceBase(const char* p_what)
	{
		static const bool enabled = std::getenv("MOON_FRAME_TRACE") != nullptr;
		if (!enabled)
		{
			return;
		}
		std::fprintf(stderr, "[base] %s\n", p_what);
		std::fflush(stderr);
	}
}
Rendering::Core::ABaseRenderer::ABaseRenderer(Context::Driver& p_driver) :
	m_driver(p_driver),
	m_isDrawing(false),
	m_emptyTexture2D{ Settings::ETextureType::TEXTURE_2D },
	m_emptyTextureCube{ Settings::ETextureType::TEXTURE_CUBE },
	m_flatNormalTexture{ Settings::ETextureType::TEXTURE_2D },
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

	m_flatNormalTexture.Allocate(kEmptyTextureDesc);
	m_flatNormalTexture.Upload(kFlatNormalPixel.data(), Settings::EFormat::RGBA, Settings::EPixelDataType::UNSIGNED_BYTE);
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
	m_presentMaterial.SetMaterial(m_presentShader);
}

void Rendering::Core::ABaseRenderer::SetBrdfLut(Rendering::HAL::Texture* p_lut)
{
	m_brdfLut = p_lut;
	if (m_perViewSet != nullptr && p_lut != nullptr && p_lut->IsValid())
	{
		m_perViewSet->SetTexture(
			Rendering::Data::EngineDescriptorSets::kBrdfLutBinding, p_lut);
	}
}

void Rendering::Core::ABaseRenderer::SetEnvironmentTextures(
	Rendering::HAL::Texture* p_irradianceCube,
	Rendering::HAL::Texture* p_prefilterCube)
{
	m_irradianceCube = p_irradianceCube;
	m_prefilterCube = p_prefilterCube;
	if (std::getenv("MOON_FRAME_TRACE") != nullptr)
	{
		// Only when the environment changes: this runs once per frame, and the interesting line is
		// the first one where the cubemaps stop being null.
		static const void* lastIrr = nullptr;
		static const void* lastPre = nullptr;
		if (static_cast<const void*>(p_irradianceCube) != lastIrr
			|| static_cast<const void*>(p_prefilterCube) != lastPre)
		{
			lastIrr = p_irradianceCube;
			lastPre = p_prefilterCube;
			std::fprintf(stderr, "[env] irr=%p id=%u pre=%p id=%u set=%p\n",
				static_cast<const void*>(p_irradianceCube),
				p_irradianceCube != nullptr ? p_irradianceCube->GetID() : 0u,
				static_cast<const void*>(p_prefilterCube),
				p_prefilterCube != nullptr ? p_prefilterCube->GetID() : 0u,
				static_cast<void*>(m_perViewSet));
			std::fflush(stderr);
		}
	}
	if (m_perViewSet == nullptr)
	{
		return;
	}
	// MOON_NO_IBL: bisect switch - every lit surface reads a flat environment instead of the sky
	// the capture baked. Two modes that disagree only with the captured cubes in place have their
	// difference in the capture, not in the shading.
	static const bool noIbl = std::getenv("MOON_NO_IBL") != nullptr;
	// Recorded unconditionally: the descriptor names the texture and resolves it when the set is
	// bound, i.e. on the executor, after the sky capture of this same batch has created it. Asking
	// IsValid() here was a read of an execution result at record time - with the batch run late it
	// was false, the bind was skipped, every lit surface lost the environment term and the frame
	// came out darker (docs/RenderThreadPlan.md §8.71).
	if (p_irradianceCube != nullptr && m_perViewSet != nullptr)
	{
		m_perViewSet->SetTexture(
			Rendering::Data::EngineDescriptorSets::kIrradianceCubeBinding,
			noIbl ? &m_emptyTextureCube : p_irradianceCube);
	}
	if (p_prefilterCube != nullptr && m_perViewSet != nullptr)
	{
		m_perViewSet->SetTexture(
			Rendering::Data::EngineDescriptorSets::kPrefilterCubeBinding,
			noIbl ? &m_emptyTextureCube : p_prefilterCube);
	}
}

void Rendering::Core::ABaseRenderer::BindPerViewDescriptorSet() const
{
	if (m_perViewSet != nullptr)
	{
		m_perViewSet->BindFixed(Rendering::Data::kPerViewDescriptorSet);
	}
}

void Rendering::Core::ABaseRenderer::BeginFrame(const Data::FrameDescriptor& p_frameDescriptor)
{
	ZoneScoped;

	// The "already bound" material cache is the driver's now (it describes GL state) and it is
	// reset by the frame's MakeCurrent command, on the side that owns the context.
	m_frameDescriptor = p_frameDescriptor;

	if (p_frameDescriptor.outputMsaaBuffer)
	{
		m_driver.BindFramebuffer(&p_frameDescriptor.outputMsaaBuffer.value());
	}

	TraceBase("base pso");
	m_basePipelineState = m_driver.CreatePipelineState();
	TraceBase("pso done");
	SetViewport(0, 0, p_frameDescriptor.renderWidth, p_frameDescriptor.renderHeight);

	Clear(
		p_frameDescriptor.camera->GetClearColorBuffer(),
		p_frameDescriptor.camera->GetClearDepthBuffer(),
		p_frameDescriptor.camera->GetClearStencilBuffer(),
		{ p_frameDescriptor.camera.value().GetClearColor(), 1.0f }
	);

	TraceBase("clear done");
	p_frameDescriptor.camera->CacheMatrices(p_frameDescriptor.renderWidth, p_frameDescriptor.renderHeight);
	TraceBase("matrices done");

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

	if (std::getenv("MOON_FRAME_TRACE") != nullptr)
	{
		// How many drawables the frame recorded: a solid that "disappears" between runs shows up
		// here first (an empty or short list) before it shows up in a screenshot.
		std::fprintf(stderr, "[frame] entityDraws=%u\n", s_entityDraws.exchange(0));
		std::fflush(stderr);
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
	Blit(p_pso, p_src, p_dst, p_material.DefaultInstance(), p_flags);
}

void Rendering::Core::ABaseRenderer::Blit(
	Rendering::Data::PipelineState p_pso,
	Rendering::HAL::Framebuffer& p_src,
	Rendering::HAL::Framebuffer& p_dst,
	Rendering::Data::MaterialInstance& p_material,
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
	blit.material = &p_material;

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

	m_driver.BindFramebuffer(&p_dst);

	if (Rendering::Settings::IsFlagSet(Rendering::Settings::EBlitFlags::UPDATE_VIEWPORT_SIZE, p_flags))
	{
		const auto [dstWidth, dstHeight] = p_dst.GetSize();
		SetViewport(0, 0, dstWidth, dstHeight);
	}

	DrawEntity(p_pso, blit);
	m_driver.BindFramebuffer(nullptr);
}

void Rendering::Core::ABaseRenderer::Present(Rendering::HAL::Framebuffer& p_src)
{
	Present(p_src, m_presentMaterial.DefaultInstance());
}
void Rendering::Core::ABaseRenderer::Present(
	Rendering::HAL::Framebuffer& p_src,
	Rendering::Data::Material& mat)
{
	Present(p_src, mat.DefaultInstance());
}
void Rendering::Core::ABaseRenderer::Present(
	Rendering::HAL::Framebuffer& p_src,
	Rendering::Data::MaterialInstance& mat)
{
	const auto colorTex = p_src.GetAttachment<HAL::Texture>(Settings::EFramebufferAttachment::COLOR);
	Present(colorTex.value(), mat);
}
void Rendering::Core::ABaseRenderer::Present(
	Rendering::HAL::Texture& p_src,
	Rendering::Data::Material& mat)
{
	Present(p_src, mat.DefaultInstance());
}
void Rendering::Core::ABaseRenderer::Present(
	Rendering::HAL::Texture& p_src,
	Rendering::Data::MaterialInstance& mat)
{
	ZoneScoped;
	mat.SetProperty("_InputTexture", &p_src);
	Present(mat);
}
void Rendering::Core::ABaseRenderer::Present(Rendering::Data::Material& mat)
{
	Present(mat.DefaultInstance());
}
void Rendering::Core::ABaseRenderer::Present(Rendering::Data::MaterialInstance& mat)
{
	ZoneScoped;


	
	Rendering::Entities::Drawable blit;
	blit.mesh = m_unitQuad;
	blit.material = &mat;
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

		material->Bind();
		// Bind() only selects the program now; callers that do not go through
		// DrawEntity have to push the uniforms themselves, otherwise the
		// samplers keep their default value and the blit samples garbage.
		material->UploadProperties();
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
	++s_entityDraws;
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

	// The material binding - program, descriptors, uniform uploads - is a command now: it is
	// recorded here and replayed by the driver, together with the pipeline state and the draw
	// call below, as one batch. Every draw binds and uploads; the "already bound" signature cache
	// is gone (see docs/RenderThreadPlan.md §8.54).
	//
	// The request owns a copy of the drawable: the caller is usually a pass loop that iterates the
	// frame's drawable list by value, so `&p_drawable` names a stack object that is gone long
	// before the batch runs (a whole frame later with a render thread) - the material and the
	// feature override would then be read out of whatever the stack holds by then.
	::Rendering::Backend::DrawMaterialRequest materialRequest;
	auto ownedDrawable = std::make_shared<::Rendering::Entities::Drawable>(p_drawable);
	materialRequest.drawable = ownedDrawable.get();
	materialRequest.ownedDrawable = ownedDrawable;
	materialRequest.pipelineState = p_pso;
	materialRequest.emptyTexture2D = &m_emptyTexture2D;
	materialRequest.emptyTextureCube = &m_emptyTextureCube;
	materialRequest.flatNormalTexture = &m_flatNormalTexture;
	materialRequest.perRenderableSet = m_perRenderableSet;
	materialRequest.perRenderableBinding = Rendering::Data::kPerRenderableDescriptorSet;
	m_driver.EnqueueDrawMaterial(materialRequest);

	m_driver.Draw(
		p_pso,
		p_drawable.mesh.value(),
		p_drawable.subIndexBufferIndex,
		p_drawable.primitiveMode,
		p_drawable.material->GetGPUInstances()
	);

	// Unbind() only clears the instance's "program in use" bookkeeping; the GL side is the
	// executor's business.
	p_drawable.material->Unbind();
}
