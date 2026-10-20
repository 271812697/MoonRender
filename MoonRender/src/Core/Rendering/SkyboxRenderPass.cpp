#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <ranges>
#include <stb_image/stb_image.h>
#include <Maths/FVector4.h>
#include <Tools/Utils/PathParser.h>
#include <Rendering/Settings/EBindingPoint.h>
#include <Core/ECS/Components/CMaterialRenderer.h>
#include <Core/Global/ServiceLocator.h>
#include <Core/Rendering/EngineBufferRenderFeature.h>
#include <Core/Rendering/EngineDrawableDescriptor.h>
#include <Core/Rendering/SkyBoxRenderPass .h>
#include <Core/Rendering/ReflectionRenderPass.h>
#include <Core/Rendering/SceneRenderer.h>
#include <Core/ResourceManagement/ShaderManager.h>
#include <Core/ResourceManagement/TextureManager.h>
#include <Core/Rendering/FramebufferUtil.h>
#include <Rendering/HAL/Renderbuffer.h>
#include <Rendering/HAL/Profiling.h>
#include <Rendering/Resources/Loaders/TextureLoader.h>

namespace
{
	unsigned int resolution = 1024;
	constexpr uint32_t kFaceCount = 6;
	const Maths::FVector3 kFaceRotations[kFaceCount] = {
		{ 0.0f, -90.0f, 180.0f },	// (Right)
		{ 0.0f, 90.0f, 180.0f },	// (Left)
		{ 90.0f, 0.0f, 180.0f },	// (Top)
		{ -90.0f, 0.0f, 180.0f },	// (Bottom)
		{ 0.0f, 0.0f, 180.0f },		// (Front)
		{ 0.0f, -180.0f, 180.0f }	// (Back)
	};
}

Core::Rendering::SkyboxRenderPass::SkyboxRenderPass(::Rendering::Core::CompositeRenderer& p_renderer) :
	::Rendering::Core::ARenderPass(p_renderer)
{
	m_skyboxMaterial.SetShader(GetShaderService[":Shaders\\SkyboxConvert.mat"]);
	m_skyboxMaterial.SetBackfaceCulling(false);
	m_skyboxIrrandianceMaterial.SetShader(GetShaderService[":Shaders\\SkyboxIrrandiance.mat"]);
	m_skyboxIrrandianceMaterial.SetBackfaceCulling(false);
	m_skyboxPrefilterMaterial.SetShader(GetShaderService[":Shaders\\SkyboxPrefilter.mat"]);
	m_skyboxPrefilterMaterial.SetBackfaceCulling(false);
	m_brdfMaterial.SetShader(GetShaderService[":Shaders\\BrdfLut.mat"]);
	m_clearMaterial.SetShader(GetShaderService[":Shaders\\ClearBackground.mat"]);
}

::Rendering::HAL::Texture* Core::Rendering::SkyboxRenderPass::GetSkyBoxCube()
{
	return skyBoxCube.get();
}

::Rendering::HAL::Texture* Core::Rendering::SkyboxRenderPass::GetIrradianceCube()
{
	return irradianceCube.get();
}

::Rendering::HAL::Texture* Core::Rendering::SkyboxRenderPass::GetPrefilterCube()
{
	return prefilterCube.get();
}

::Rendering::HAL::Texture* Core::Rendering::SkyboxRenderPass::GetBrdfTexture()
{
	auto attachment = brdfBuffer.GetAttachment<::Rendering::HAL::GLTexture>(::Rendering::Settings::EFramebufferAttachment::COLOR, 0);
	return attachment.has_value() ? &attachment.value() : nullptr;
}
	

void Core::Rendering::SkyboxRenderPass::Draw(::Rendering::Data::PipelineState p_pso)
{
	ZoneScoped;
	TracyGpuZone("SkyboxRenderPass");
	if (m_irradianceSHBuffer)
	{
		m_irradianceSHBuffer->Bind(::Rendering::Settings::kIrradianceSHUBOBinding);
	}
	if (needUpdateSkyTexture && m_captureEnvironment) {
		needUpdateSkyTexture = false;
		computeSkyTexture();
	}	
	auto& engineBufferRenderFeature = m_renderer.GetFeature<Core::Rendering::EngineBufferRenderFeature>();
	auto& frameDescriptor = m_renderer.GetFrameDescriptor();
	if (auto output = frameDescriptor.outputMsaaBuffer)
	{
		output.value().Bind();
	}
	// computeSkyTexture() overwrites the engine UBO with the sky capture camera,
	// so the real camera must always be restored regardless of the sky mode.
	engineBufferRenderFeature.SetCamera(frameDescriptor.camera.value());
	//draw skybox
	if(mSetting.mode==SkyMode::SkyBox)
	{
		//visible skybox
		m_skyboxMaterial.RemoveFeature("SKY_Convert");

		// Draw skybox
		::Rendering::Entities::Drawable skyboxDrawable;
		skyboxDrawable.mesh = m_renderer.m_unitCube;
		skyboxDrawable.material = m_skyboxMaterial;
		skyboxDrawable.stateMask.depthWriting = false;
		skyboxDrawable.stateMask.colorWriting = true;
		skyboxDrawable.stateMask.blendable = false;
		skyboxDrawable.stateMask.frontfaceCulling = false;
		skyboxDrawable.stateMask.backfaceCulling = false;
		skyboxDrawable.stateMask.depthTest = false;
		skyboxDrawable.material.value().SetProperty("SkyboxCube", prefilterCube.get());
		m_renderer.DrawEntity(p_pso, skyboxDrawable);	
	}
	else if (mSetting.mode == SkyMode::PureColor) {
		m_clearMaterial.SetFeatures({ "PureColor" });
		m_clearMaterial.SetProperty("uClearColor",mSetting.clearColor);
		m_renderer.Present(m_clearMaterial);
	}
	else if(mSetting.mode == SkyMode::Gradient)
	{
		m_clearMaterial.SetFeatures({  });
		m_clearMaterial.SetProperty("uClearColor", mSetting.clearColor);
		m_clearMaterial.SetProperty("uTopColor", mSetting.topColor);
		m_clearMaterial.SetProperty("uBottomColor", mSetting.bottomColor);
		m_renderer.Present(m_clearMaterial);
	}
}

void Core::Rendering::SkyboxRenderPass::computeIrradianceSH()
{
	// Resolve the equirect sky the bake came from (the resource key mPath).
	std::string key = mPath;
	if (!key.empty() && key.front() == ':') { key.erase(key.begin()); }
	std::replace(key.begin(), key.end(), '\\', '/');

	const std::string roots[] = {
		Tools::Utils::PathParser::GetExeDirectory() + "/Moon/Data/Engine/",
		"Resource/Moon/Data/Engine/",
		"Moon/Data/Engine/"
	};
	std::string resolved;
	for (const std::string& root : roots)
	{
		const std::string candidate = root + key;
		if (std::filesystem::exists(candidate))
		{
			resolved = candidate;
			break;
		}
	}
	if (resolved.empty()) { std::cout << "[Skybox] cannot resolve " << mPath << " for SH" << std::endl; return; }

	int width = 0;
	int height = 0;
	int channels = 0;
	// The sky texture reaches the GPU through TextureLoader, which leaves stb's global vertical
	// flip enabled: the uploaded texture's first row is the bottom of the equirect. Read the
	// projection from the same rows, or the SH lights the scene from an upside-down sky.
	stbi_set_flip_vertically_on_load(true);
	float* pixels = stbi_loadf(resolved.c_str(), &width, &height, &channels, 4);
	if (pixels == nullptr || width <= 0 || height <= 0) { std::cout << "[Skybox] cannot load " << resolved << " for SH" << std::endl; return; }

	// Orthonormal SH9 basis, the same order EvaluateIrradianceSH uses.
	constexpr float kPi = 3.14159265358979323846f;
	// FVector3 has a user-provided default constructor, so `{}` does not zero it here:
	// the coefficients must start at exactly zero.
	Maths::FVector3 sh[9];
	for (int i = 0; i < 9; ++i) { sh[i] = Maths::FVector3{ 0.0f, 0.0f, 0.0f }; }
	for (int y = 0; y < height; ++y)
	{
		// Row 0 is the bottom of the equirect (stb's flip; see above), so latitude runs from
		// -PI/2 at row 0 to +PI/2 at the last row. Azimuth is Filament's toEquirect() mapping,
		// u = 0.5 + atan2(x, z) / 2PI.
		const float yf = 2.0f * ((y + 0.5f) / height) - 1.0f;
		const float lat = yf * 0.5f * kPi;
		const float sinLat = std::sin(lat);
		const float cosLat = std::cos(lat);
		for (int x = 0; x < width; ++x)
		{
			const float xf = 2.0f * ((x + 0.5f) / width) - 1.0f;
			const float phi = xf * kPi;
			// Filament's IBL cube is mirrored on X (see SkyboxConvert.mat): the texel whose
			// equirect direction is (px, py, pz) is stored at (-px, py, pz). Project that
			// direction, so the SH describes the same environment the specular cube holds.
			const float px = -cosLat * std::sin(phi);
			const float py = sinLat;
			const float pz = cosLat * std::cos(phi);

			const float* texel = pixels + (y * width + x) * 4;
			// An environment cannot have negative radiance; a decoded HDR that does is treated
			// as black there instead of letting it cancel the positive samples.
			const Maths::FVector3 radiance{ std::max(texel[0], 0.0f), std::max(texel[1], 0.0f), std::max(texel[2], 0.0f) };
			const float dOmega = cosLat * (kPi / height) * (2.0f * kPi / width);

			const float basis[9] = {
				0.282095f,
				0.488603f * py, 0.488603f * pz, 0.488603f * px,
				1.092548f * px * py, 1.092548f * py * pz,
				0.315392f * (3.0f * pz * pz - 1.0f),
				1.092548f * px * pz, 0.546274f * (px * px - py * py)
			};
			for (int i = 0; i < 9; ++i)
			{
				sh[i] += radiance * (basis[i] * dOmega);
			}
		}
	}

	stbi_image_free(pixels);

	// Cosine-convolved irradiance E (the same convention as SkyboxIrrandiance.mat and
	// therefore the existing IBL brightness): band scales PI, 2PI/3, PI/4.
	for (int i = 0; i < 9; ++i)
	{
		sh[i] *= (i == 0) ? kPi : (i < 4 ? (2.0f * kPi / 3.0f) : (kPi / 4.0f));
	}

	if (!m_irradianceSHBuffer)
	{
		m_irradianceSHBuffer = std::make_unique<::Rendering::HAL::UniformBuffer>();
		m_irradianceSHBuffer->Allocate(9 * sizeof(Maths::FVector4), ::Rendering::Settings::EAccessSpecifier::STATIC_DRAW);
	}

	Maths::FVector4 data[9];
	for (int i = 0; i < 9; ++i)
	{
		data[i] = Maths::FVector4{ sh[i].x, sh[i].y, sh[i].z, i == 0 ? 1.0f : 0.0f };
	}
	m_irradianceSHBuffer->Upload(data);
	m_irradianceSHBuffer->Bind(::Rendering::Settings::kIrradianceSHUBOBinding);
}

void Core::Rendering::SkyboxRenderPass::computeSkyTexture()
{
	using namespace Core::Rendering;
	auto p_pso =m_renderer.CreatePipelineState();
	skyTexture = GetService(Core::ResourceManagement::TextureManager).GetResource(mPath);
	
	//if (!irradianceCube.get())
	{
		//set up framebuffer
		{
			//initialize framebuffer
			{
				auto fBColorDesc = ::Rendering::Settings::TextureDesc{
				.width = resolution,
				.height = resolution,
				.minFilter = ::Rendering::Settings::ETextureFilteringMode::LINEAR,
				.magFilter = ::Rendering::Settings::ETextureFilteringMode::LINEAR,
				.horizontalWrap = ::Rendering::Settings::ETextureWrapMode::CLAMP_TO_EDGE,
				.verticalWrap = ::Rendering::Settings::ETextureWrapMode::CLAMP_TO_EDGE,
				.internalFormat = ::Rendering::Settings::EInternalFormat::RG32F,
				.useMipMaps = false,
				.mutableDesc = ::Rendering::Settings::MutableTextureDesc{
					.format = ::Rendering::Settings::EFormat::RG,
					.type = ::Rendering::Settings::EPixelDataType::FLOAT
				}
				};

				::Core::Rendering::FramebufferUtil::SetupFramebuffer(brdfBuffer, fBColorDesc, false, false, false);

				

				skyBoxCube = std::make_shared<::Rendering::HAL::Texture>(
					::Rendering::Settings::ETextureType::TEXTURE_CUBE,
					"skyBoxCube"
				);
				skyBoxCube->Allocate(
					::Rendering::Settings::TextureDesc{
						.width = resolution,
						.height = resolution,
						.minFilter = ::Rendering::Settings::ETextureFilteringMode::LINEAR_MIPMAP_LINEAR,
							.magFilter = ::Rendering::Settings::ETextureFilteringMode::LINEAR,
							.horizontalWrap = ::Rendering::Settings::ETextureWrapMode::CLAMP_TO_EDGE,
							.verticalWrap = ::Rendering::Settings::ETextureWrapMode::CLAMP_TO_EDGE,
							.internalFormat = ::Rendering::Settings::EInternalFormat::RGBA32F,
							.useMipMaps = true
					}
				);

				irradianceCube = std::make_shared<::Rendering::HAL::Texture>(
					::Rendering::Settings::ETextureType::TEXTURE_CUBE,
					"irradianceCube"
				);

				irradianceCube->Allocate(
					::Rendering::Settings::TextureDesc{
						.width = 32,
						.height = 32,
						.minFilter = ::Rendering::Settings::ETextureFilteringMode::LINEAR_MIPMAP_LINEAR,
						.magFilter = ::Rendering::Settings::ETextureFilteringMode::LINEAR,
						.horizontalWrap = ::Rendering::Settings::ETextureWrapMode::CLAMP_TO_EDGE,
						.verticalWrap = ::Rendering::Settings::ETextureWrapMode::CLAMP_TO_EDGE,
						.internalFormat = ::Rendering::Settings::EInternalFormat::RGBA32F,
						.useMipMaps = true
					}
				);
				prefilterCube = std::make_shared<::Rendering::HAL::Texture>(
					::Rendering::Settings::ETextureType::TEXTURE_CUBE,
					"prefilterCube"
				);
				prefilterCube->Allocate(
					::Rendering::Settings::TextureDesc{
						.width = resolution,
						.height = resolution,
						.minFilter = ::Rendering::Settings::ETextureFilteringMode::LINEAR_MIPMAP_LINEAR,
						.magFilter = ::Rendering::Settings::ETextureFilteringMode::LINEAR,
						.horizontalWrap = ::Rendering::Settings::ETextureWrapMode::CLAMP_TO_EDGE,
						.verticalWrap = ::Rendering::Settings::ETextureWrapMode::CLAMP_TO_EDGE,
						.internalFormat = ::Rendering::Settings::EInternalFormat::RGBA32F,
						.useMipMaps = true
					}
				);
				prefilterCube->GenerateMipmaps();
			}
			//set up skyBoxBuffer 
			{
				for (uint32_t faceIndex = 0; faceIndex < 6; ++faceIndex)
				{
					skyBoxBuffer.Attach<::Rendering::HAL::Texture>(
						skyBoxCube,
						::Rendering::Settings::EFramebufferAttachment::COLOR,
						faceIndex, // Each color attachment is a face of the cubemap
						faceIndex // Each face of the cubemap is accessed by its layer index
					);
				}
				// Depth buffer
				const auto renderbuffer = std::make_shared<::Rendering::HAL::Renderbuffer>(false);
				const auto internalFormat = ::Rendering::Settings::EInternalFormat::DEPTH_COMPONENT;
				renderbuffer->Allocate(resolution, resolution, internalFormat);
				skyBoxBuffer.Attach(renderbuffer, ::Rendering::Settings::EFramebufferAttachment::DEPTH);
				// Validation
				skyBoxBuffer.Validate();
			}
			//set up irradianceBuffer
			{
				for (uint32_t faceIndex = 0; faceIndex < 6; ++faceIndex)
				{
					irradianceBuffer.Attach<::Rendering::HAL::Texture>(
						irradianceCube,
						::Rendering::Settings::EFramebufferAttachment::COLOR,
						faceIndex, // Each color attachment is a face of the cubemap
						faceIndex // Each face of the cubemap is accessed by its layer index
					);
				}

				// Depth buffer (must match the 32x32 irradiance cube size, otherwise the FBO is incomplete)
				const auto renderbuffer = std::make_shared<::Rendering::HAL::Renderbuffer>(false);
				const auto internalFormat = ::Rendering::Settings::EInternalFormat::DEPTH_COMPONENT;
				renderbuffer->Allocate(
					static_cast<uint16_t>(irradianceCube->GetDesc().width),
					static_cast<uint16_t>(irradianceCube->GetDesc().height),
					internalFormat
				);
				irradianceBuffer.Attach(renderbuffer, ::Rendering::Settings::EFramebufferAttachment::DEPTH);
				// Validation
				irradianceBuffer.Validate();
			}
			//set up prefilterBuffer
			{
				for (uint32_t faceIndex = 0; faceIndex < 6; ++faceIndex)
				{
					prefilterBuffer.Attach<::Rendering::HAL::Texture>(
						prefilterCube,
						::Rendering::Settings::EFramebufferAttachment::COLOR,
						faceIndex, // Each color attachment is a face of the cubemap
						faceIndex // Each face of the cubemap is accessed by its layer index
					);
				}

				// No depth buffer here: each prefilter mip has a different size, so the depth
				// renderbuffer is attached per-mip inside the prefilter loop below.
				// Validation
				prefilterBuffer.Validate();
			}
		}
	}


	//precompute ibl
	{
		//convert a 2D hdr texture to sky box
		{
			auto& engineBufferRenderFeature = m_renderer.GetFeature<Core::Rendering::EngineBufferRenderFeature>();
			auto& frameDescriptor = m_renderer.GetFrameDescriptor();
			m_skyboxMaterial.AddFeature("SKY_Convert");

			::Rendering::Entities::Camera skyCamera;
			skyCamera.SetPosition({ 0.0f, 0.0f, 0.0f });
			skyCamera.SetFov(90.0f);
			const auto [width, height] = skyBoxBuffer.GetSize();
			skyBoxBuffer.Bind();
			m_renderer.SetViewport(0, 0, width, height);
			for (uint32_t faceIndex = 0; faceIndex < 6; ++faceIndex)
			{
				skyCamera.SetRotation(Maths::FQuaternion{ kFaceRotations[faceIndex] });
				skyCamera.CacheMatrices(width, height);
				engineBufferRenderFeature.SetCamera(skyCamera);
				skyBoxBuffer.SetTargetDrawBuffer(faceIndex);
				m_renderer.Clear(true, true, true);
				// Draw skybox
				::Rendering::Entities::Drawable skyboxDrawable;
				skyboxDrawable.mesh = m_renderer.m_unitCube;
				skyboxDrawable.material = m_skyboxMaterial;
				auto stateMask = m_skyboxMaterial.GenerateStateMask();
				skyboxDrawable.stateMask = stateMask;
				skyboxDrawable.material.value().SetProperty("u_SkyBoxTexture", skyTexture);
				m_renderer.DrawEntity(p_pso, skyboxDrawable);
			}
			skyBoxBuffer.Unbind();
			m_renderer.SetViewport(0, 0, frameDescriptor.renderWidth, frameDescriptor.renderHeight);
			//it is important to do this at this point because we need the mipmaps for the prefilter
			skyBoxCube->GenerateMipmaps();
		}
		//convolution
		{
			auto& engineBufferRenderFeature = m_renderer.GetFeature<Core::Rendering::EngineBufferRenderFeature>();
			auto& frameDescriptor = m_renderer.GetFrameDescriptor();


			::Rendering::Entities::Camera skyCamera;
			skyCamera.SetPosition({ 0.0f, 0.0f, 0.0f });
			skyCamera.SetFov(90.0f);
			const auto [width, height] = irradianceBuffer.GetSize();
			irradianceBuffer.Bind();
			m_renderer.SetViewport(0, 0, width, height);
			for (uint32_t faceIndex = 0; faceIndex < 6; ++faceIndex)
			{
				skyCamera.SetRotation(Maths::FQuaternion{ kFaceRotations[faceIndex] });
				skyCamera.CacheMatrices(width, height);
				engineBufferRenderFeature.SetCamera(skyCamera);
				irradianceBuffer.SetTargetDrawBuffer(faceIndex);
				m_renderer.Clear(true, true, true);
				// Draw skybox
				::Rendering::Entities::Drawable skyboxDrawable;
				skyboxDrawable.mesh = m_renderer.m_unitCube;
				skyboxDrawable.material = m_skyboxIrrandianceMaterial;
				auto stateMask = m_skyboxIrrandianceMaterial.GenerateStateMask();
				skyboxDrawable.stateMask = stateMask;
				skyboxDrawable.material.value().SetProperty("SkyboxCube", skyBoxCube.get());
				skyboxDrawable.material.value().SetProperty("u_CaptureAxis", static_cast<int>(faceIndex / 2));
				skyboxDrawable.material.value().SetProperty("u_CaptureSide", (faceIndex % 2 == 0) ? 1.0f : -1.0f);
				skyboxDrawable.material.value().SetProperty("u_CaptureViewport",
					Maths::FVector2{ static_cast<float>(irradianceCube->GetDesc().width),
						static_cast<float>(irradianceCube->GetDesc().height) });
				m_renderer.DrawEntity(p_pso, skyboxDrawable);
			}
			irradianceBuffer.Unbind();
			// The irradiance cube is sampled with implicit LOD (mipmapped min filter),
			// so the mip chain must be generated after the convolution.
			irradianceCube->GenerateMipmaps();
			computeIrradianceSH();
			m_renderer.SetViewport(0, 0, frameDescriptor.renderWidth, frameDescriptor.renderHeight);
		}
		//prefilter
		{
			auto& engineBufferRenderFeature = m_renderer.GetFeature<Core::Rendering::EngineBufferRenderFeature>();
			auto& frameDescriptor = m_renderer.GetFrameDescriptor();

			::Rendering::Entities::Camera skyCamera;
			skyCamera.SetPosition({ 0.0f, 0.0f, 0.0f });
			skyCamera.SetFov(90.0f);
			const auto [width, height] = prefilterBuffer.GetSize();
			const uint32_t maxMipLevels = static_cast<uint32_t>(std::floor(std::log2(width))) + 1u;
			prefilterBuffer.Bind();
			for (uint32_t mip = 0; mip < maxMipLevels; ++mip)
			{
				uint32_t mipWidth = static_cast<uint32_t>(width * std::pow(0.5f, mip));
				uint32_t mipHeight = static_cast<uint32_t>(height * std::pow(0.5f, mip));
				m_renderer.SetViewport(0, 0, mipWidth, mipHeight);
				// Filament's prefilter bakes level/maxLod with the inverse of
				// perceptualRoughnessToLod(): p = 1 - sqrt(1 - level/maxLod). The shaders sample
				// with the forward curve, so a surface of perceptual roughness p lands on a mip
				// that was baked for exactly that roughness.
				const float levelFraction = (float)mip / (float)(maxMipLevels - 1);
				const float roughness = 1.0f - std::sqrt(std::max(0.0f, 1.0f - levelFraction));

				// Depth buffer must match the current mip size, otherwise the FBO is incomplete.
				const auto renderbuffer = std::make_shared<::Rendering::HAL::Renderbuffer>(false);
				renderbuffer->Allocate(
					static_cast<uint16_t>(mipWidth),
					static_cast<uint16_t>(mipHeight),
					::Rendering::Settings::EInternalFormat::DEPTH_COMPONENT
				);
				prefilterBuffer.Attach(renderbuffer, ::Rendering::Settings::EFramebufferAttachment::DEPTH);

				for (uint32_t faceIndex = 0; faceIndex < 6; ++faceIndex)
				{
					skyCamera.SetRotation(Maths::FQuaternion{ kFaceRotations[faceIndex] });
					skyCamera.CacheMatrices(mipWidth, mipHeight);
					engineBufferRenderFeature.SetCamera(skyCamera);

					prefilterBuffer.AttachLevel<::Rendering::HAL::Texture>(
						prefilterCube,
						::Rendering::Settings::EFramebufferAttachment::COLOR,
						faceIndex, // Each color attachment is a face of the cubemap
						mip,
						faceIndex // Each face of the cubemap is accessed by its layer index
					);
					prefilterBuffer.SetTargetDrawBuffer(faceIndex);
					prefilterBuffer.Validate();
					m_renderer.Clear(true, true, true);
					// Draw skybox
					::Rendering::Entities::Drawable skyboxDrawable;
					skyboxDrawable.mesh = m_renderer.m_unitCube;
					skyboxDrawable.material = m_skyboxPrefilterMaterial;
					auto stateMask = m_skyboxPrefilterMaterial.GenerateStateMask();
					skyboxDrawable.stateMask = stateMask;
					skyboxDrawable.material.value().SetProperty("SkyboxCube", skyBoxCube.get());
					skyboxDrawable.material.value().SetProperty("roughness", roughness);
					// Filament's per-level lodOffset: 1.0 (2.0 on the last level) minus the
					// solid angle of an output texel, so the sample LOD lands in output-texel
					// units rather than sample-density units.
					constexpr float kPi = 3.14159265358979323846f;
					const float omegaP = 4.0f * kPi / (6.0f * float(mipWidth) * float(mipWidth));
					const float lodOffset =
						((mip == maxMipLevels - 1) ? 2.0f : 1.0f) - 0.5f * std::log2(omegaP);
					skyboxDrawable.material.value().SetProperty("lodOffset", lodOffset);
					skyboxDrawable.material.value().SetProperty("u_CaptureAxis", static_cast<int>(faceIndex / 2));
					skyboxDrawable.material.value().SetProperty("u_CaptureSide", (faceIndex % 2 == 0) ? 1.0f : -1.0f);
					skyboxDrawable.material.value().SetProperty("u_CaptureViewport",
						Maths::FVector2{ static_cast<float>(mipWidth), static_cast<float>(mipHeight) });
					m_renderer.DrawEntity(p_pso, skyboxDrawable);
				}
			}
			prefilterBuffer.Unbind();
			m_renderer.SetViewport(0, 0, frameDescriptor.renderWidth, frameDescriptor.renderHeight);
		}
		//brdf lut
		{
			auto& frameDescriptor = m_renderer.GetFrameDescriptor();

			::Rendering::Entities::Drawable blit;
			blit.mesh = m_renderer.m_unitQuad;
			blit.material = m_brdfMaterial;
			blit.stateMask.depthWriting = false;
			blit.stateMask.colorWriting = true;
			blit.stateMask.blendable = false;
			blit.stateMask.frontfaceCulling = false;
			blit.stateMask.backfaceCulling = false;
			blit.stateMask.depthTest = false;
			const auto [width, height] = brdfBuffer.GetSize();
			brdfBuffer.Bind();
			m_renderer.SetViewport(0, 0, width, height);
			m_renderer.DrawEntity(p_pso, blit);
			brdfBuffer.Unbind();
			m_renderer.SetViewport(0, 0, frameDescriptor.renderWidth, frameDescriptor.renderHeight);
		}
	}
}

void Core::Rendering::SkyboxRenderPass::updateSkyTexture(const std::string& path)
{
	needUpdateSkyTexture = true;
	mPath = path;
}
