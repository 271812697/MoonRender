#include <ranges>
#include <Core/ECS/Components/CMaterialRenderer.h>
#include <Core/Global/ServiceLocator.h>
#include <Core/Rendering/EngineBufferRenderFeature.h>
#include <Core/Rendering/EngineDrawableDescriptor.h>
#include <Core/Rendering/GbufferPass.h>
#include <Core/Rendering/ReflectionRenderPass.h>
#include <Core/Rendering/SceneRenderer.h>
#include <Core/ResourceManagement/ShaderManager.h>
#include <Core/ResourceManagement/TextureManager.h>
#include <Core/Rendering/FramebufferUtil.h>
#include <Rendering/HAL/Renderbuffer.h>
#include <Rendering/HAL/Profiling.h>
#include <Rendering/Resources/Loaders/TextureLoader.h>
#include <random>

static std::vector<Maths::FVector3> ssaoKernel;
static std::vector<Maths::FVector3> ssaoNoise;
static float ourLerp(float a, float b, float f)
{
	return a + f * (b - a);
}

Core::Rendering::GbufferPass::GbufferPass(::Rendering::Core::CompositeRenderer& p_renderer) :
	::Rendering::Core::ARenderPass(p_renderer)
{
	gbufferMaterial.SetShader(GetShaderService[":Shaders\\gbuffer.shader"]);
	gbufferMaterial.SetBackfaceCulling(false);
	ssaoMaterial.SetShader(GetShaderService[":Shaders\\ssao.shader"]);
	ssaoMaterial.SetBackfaceCulling(false);
	ssaoblurMaterial.SetShader(GetShaderService[":Shaders\\ssaoblur.shader"]);
	//ssaoblurMaterial.AddFeature("k15x15");

	{
		::Rendering::Settings::TextureDesc colorDesc{
				.width = 1,
				.height = 1,
				.minFilter = ::Rendering::Settings::ETextureFilteringMode::NEAREST,
				.magFilter = ::Rendering::Settings::ETextureFilteringMode::NEAREST,
				.horizontalWrap = ::Rendering::Settings::ETextureWrapMode::CLAMP_TO_BORDER,
				.verticalWrap = ::Rendering::Settings::ETextureWrapMode::CLAMP_TO_BORDER,
				.internalFormat = ::Rendering::Settings::EInternalFormat::RGB32F,
				.useMipMaps = false,
				.mutableDesc = ::Rendering::Settings::MutableTextureDesc{
					.format = ::Rendering::Settings::EFormat::RGB,
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

		gbufferData.position = std::make_shared<::Rendering::HAL::Texture>(
			::Rendering::Settings::ETextureType::TEXTURE_2D, "position");
		gbufferData.position->Allocate(colorDesc);
		gbufferData.normal = std::make_shared<::Rendering::HAL::Texture>(
			::Rendering::Settings::ETextureType::TEXTURE_2D, "normal");
		gbufferData.normal->Allocate(colorDesc);
		auto depthTexture = std::make_shared<::Rendering::HAL::Texture>(
			::Rendering::Settings::ETextureType::TEXTURE_2D, "depth");
		depthTexture->Allocate(depthDesc);
		depthTexture->SetBorderColor(Maths::FVector4::One);
		gbuffer.Attach<::Rendering::HAL::Texture>(gbufferData.position, ::Rendering::Settings::EFramebufferAttachment::COLOR,0);
		gbuffer.Attach<::Rendering::HAL::Texture>(gbufferData.normal, ::Rendering::Settings::EFramebufferAttachment::COLOR,1);
		gbuffer.Attach<::Rendering::HAL::Texture>(depthTexture, ::Rendering::Settings::EFramebufferAttachment::DEPTH);
		//gbuffer.SetTargetDrawBuffer(0);
		gbuffer.SetDrawBuffers({0,1});
		if (!gbuffer.Validate()) {
			std::cout << "invalidate buffer" << std::endl;
		}
	}

	{
		::Rendering::Settings::TextureDesc colorDesc{
		.width = 1,
		.height = 1,
		.minFilter = ::Rendering::Settings::ETextureFilteringMode::NEAREST,
		.magFilter = ::Rendering::Settings::ETextureFilteringMode::NEAREST,
		.horizontalWrap = ::Rendering::Settings::ETextureWrapMode::CLAMP_TO_BORDER,
		.verticalWrap = ::Rendering::Settings::ETextureWrapMode::CLAMP_TO_BORDER,
		.internalFormat = ::Rendering::Settings::EInternalFormat::RGBA,
		.useMipMaps = false,
		.mutableDesc = ::Rendering::Settings::MutableTextureDesc{
			.format = ::Rendering::Settings::EFormat::RGBA,
			.type = ::Rendering::Settings::EPixelDataType::FLOAT
		}
		};		
		std::uniform_real_distribution<GLfloat> randomFloats(0.0, 1.0); // generates random floats between 0.0 and 1.0
		std::default_random_engine generator;
		for (unsigned int i = 0; i < 64; ++i)
		{
			Maths::FVector3 sample(randomFloats(generator) * 2.0 - 1.0, randomFloats(generator) * 2.0 - 1.0, randomFloats(generator));
			sample = sample.Normalize();
			sample *= randomFloats(generator);
			float scale = float(i) / 64.0f;

			// scale samples s.t. they're more aligned to center of kernel
			scale = ourLerp(0.1f, 1.0f, scale * scale);
			sample *= scale;
			ssaoKernel.push_back(sample);
		}

		// generate noise texture
		for (unsigned int i = 0; i < 16; i++)
		{
			Maths::FVector3 noise(randomFloats(generator) * 2.0 - 1.0, randomFloats(generator) * 2.0 - 1.0, 0.0f); // rotate around z-axis (in tangent space)
			ssaoNoise.push_back(noise);
		}
		::Rendering::Settings::TextureDesc noiseDesc{
		.width = 4,
		.height = 4,
		.minFilter = ::Rendering::Settings::ETextureFilteringMode::NEAREST,
		.magFilter = ::Rendering::Settings::ETextureFilteringMode::NEAREST,
		.horizontalWrap = ::Rendering::Settings::ETextureWrapMode::REPEAT,
		.verticalWrap = ::Rendering::Settings::ETextureWrapMode::REPEAT,
		.internalFormat = ::Rendering::Settings::EInternalFormat::RGB32F,
		.useMipMaps = false,
		.mutableDesc = ::Rendering::Settings::MutableTextureDesc{
			.format = ::Rendering::Settings::EFormat::RGB,
			.type = ::Rendering::Settings::EPixelDataType::FLOAT,
			.data= ssaoNoise.data()
		}
		};


		noiseTexture= std::make_shared<::Rendering::HAL::Texture>(
			::Rendering::Settings::ETextureType::TEXTURE_2D, "noiseTexture");
		noiseTexture->Allocate(noiseDesc);

		gbufferData.occlusion = std::make_shared<::Rendering::HAL::Texture>(
			::Rendering::Settings::ETextureType::TEXTURE_2D, "occlusion");
		gbufferData.occlusion->Allocate(colorDesc);
		ssaobuffer.Attach<::Rendering::HAL::Texture>(gbufferData.occlusion, ::Rendering::Settings::EFramebufferAttachment::COLOR, 0);
		if (!ssaobuffer.Validate()) {
			std::cout << "invalidate buffer" << std::endl;
		}

		gbufferData.occlusionBlur = std::make_shared<::Rendering::HAL::Texture>(
			::Rendering::Settings::ETextureType::TEXTURE_2D, "occlusionBlur");
		gbufferData.occlusionBlur->Allocate(colorDesc);
		ssaoblurbuffer.Attach<::Rendering::HAL::Texture>(gbufferData.occlusionBlur, ::Rendering::Settings::EFramebufferAttachment::COLOR, 0);
		if (!ssaoblurbuffer.Validate()) {
			std::cout << "invalidate buffer" << std::endl;
		}
	}
	{
		ssaoMaterial.SetProperty("gPosition", gbufferData.position.get());
		ssaoMaterial.SetProperty("gNormal", gbufferData.normal.get());
		ssaoMaterial.SetProperty("texNoise", noiseTexture.get());
		for (unsigned int i = 0; i < 64; ++i)
			ssaoMaterial.SetProperty("samples[" + std::to_string(i) + "]", ssaoKernel[i]);
		// Depth-aware blur needs the view-space position buffer to weight taps
		// by depth similarity (bilateral filter).
		ssaoblurMaterial.SetProperty("gPosition", gbufferData.position.get());
	}
}


void Core::Rendering::GbufferPass::Draw(::Rendering::Data::PipelineState p_pso)
{
	ZoneScoped;
	TracyGpuZone("GbufferPass");
	using namespace Core::Rendering;
	//Render to gbuffer
	{
		gbuffer.Bind();
		m_renderer.Clear(true, true, false, Maths::FVector4(0, 0, 0, 1.0));
		const auto& drawables = m_renderer.GetDescriptor<SceneRenderer::SceneFilteredDrawablesDescriptor>();
		for (auto drawable : drawables.opaques | std::views::values)
		{
			//we should not draw sky box to gbuffer, but for now we just use the same pass for simplicity
			if (drawable.primitiveMode == ::Rendering::Settings::EPrimitiveMode::TRIANGLES)
			{
				drawable.pass = "";
				drawable.stateMask.depthWriting = true;
				drawable.stateMask.colorWriting = true;
				drawable.stateMask.blendable = false;
				drawable.stateMask.frontfaceCulling = false;
				drawable.stateMask.backfaceCulling = false;
				drawable.stateMask.depthTest = true;
				drawable.material = gbufferMaterial;
				m_renderer.DrawEntity(p_pso, drawable);
			}

		}
		gbuffer.Unbind();
	}

	//Render ssao
	{
		::Rendering::Entities::Drawable drawable;
		drawable.pass = "";
		// The SSAO buffer has no depth attachment, so depth testing/writing here
		// is meaningless; keep them disabled.
		drawable.stateMask.depthWriting = false;
		drawable.stateMask.colorWriting = true;
		drawable.stateMask.blendable = false;
		drawable.stateMask.frontfaceCulling = false;
		drawable.stateMask.backfaceCulling = false;
		drawable.stateMask.depthTest = false;
		drawable.material = ssaoMaterial;

		drawable.mesh = m_renderer.m_unitQuad;

		auto& frameDesc = m_renderer.GetFrameDescriptor();
		auto proj=frameDesc.camera.value().GetProjectionMatrix();
		auto view = frameDesc.camera.value().GetViewMatrix();
		

		ssaoMaterial.SetProperty("projection",proj);
		ssaoMaterial.SetProperty("view",view);
		ssaoMaterial.SetProperty("radius", gbufferParam.ssaoParam.radius);
		ssaoMaterial.SetProperty("bias", gbufferParam.ssaoParam.bias);
		ssaoMaterial.SetProperty("screensize",Maths::FVector2(frameDesc.renderWidth,frameDesc.renderHeight));;
		//compute ssao
		ssaobuffer.Bind();
		m_renderer.Clear(true, false, false, Maths::FVector4(1, 1, 1, 1.0));
		m_renderer.DrawEntity(p_pso, drawable);
		ssaobuffer.Unbind();
		//blur ssao
		ssaoblurMaterial.SetProperty("ssaoInput", gbufferData.occlusion.get());
		// Depth tolerance of the bilateral blur, in view-space units. Smaller
		// values keep AO edges sharp, larger values smooth the interior more.
		ssaoblurMaterial.SetProperty("u_BlurDepthTolerance", gbufferParam.ssaoParam.blurTolerance);
		m_renderer.Blit(p_pso, ssaobuffer,ssaoblurbuffer, ssaoblurMaterial);
	}
	

	//auto& mssaaframebuffer = m_renderer.GetFrameDescriptor().outputMsaaBuffer.value();
	//mssaaframebuffer.Bind();
	//const auto& content = ssaoblurbuffer.GetAttachment<::Rendering::HAL::GLTexture>(::Rendering::Settings::EFramebufferAttachment::COLOR, 0);
	//m_renderer.Present(content.value());
}

void Core::Rendering::GbufferPass::ResizeRenderer(int width, int height)
{
	gbuffer.Resize(width, height);
	ssaobuffer.Resize(width,height);
	ssaoblurbuffer.Resize(width, height);
}

