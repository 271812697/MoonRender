#include <ranges>
#include <tracy/Tracy.hpp>
#include <algorithm>
#include <glad/glad.h>
#include <cstdint>
#include <cstring>
#include <Core/ECS/Components/CModelRenderer.h>
#include <Core/ECS/Components/CMaterialRenderer.h>
#include <Core/Global/ServiceLocator.h>
#include <Core/Rendering/FramebufferUtil.h>
#include <Core/Rendering/EngineBufferRenderFeature.h>
#include <Core/Rendering/EngineDrawableDescriptor.h>
#include <Core/Rendering/PostProcessRenderPass.h>
#include <Core/Rendering/ReflectionRenderFeature.h>
#include <Core/Rendering/ReflectionRenderPass.h>
#include <Core/Rendering/SkyBoxRenderPass .h>
#include <Core/Rendering/SceneRenderer.h>
#include <Core/Rendering/ShadowRenderFeature.h>
#include <Core/Rendering/ShadowRenderPass.h>
#include <Core/Rendering/GbufferPass.h>
#include <Core/Rendering/HzbBuildPass.h>
#include <Core/ResourceManagement/ShaderManager.h>
#include <Rendering/Data/Frustum.h>
#include <Rendering/Features/LightingRenderFeature.h>
#include "Core/Rendering/SsaoRenderFeature.h"
#include <Rendering/HAL/Profiling.h>
#include <Rendering/Resources/Loaders/ShaderLoader.h>
#include <Rendering/HAL/Texture.h>

namespace
{
	using namespace Core::Rendering;

	/** Cheap 64-bit content signature used to cache the parsed drawables. */
	void HashCombine(uint64_t& p_seed, uint64_t p_value)
	{
		p_seed ^= p_value + 0x9e3779b97f4a7c15ULL + (p_seed << 6) + (p_seed >> 2);
	}

	uint64_t HashPointer(const void* p_pointer)
	{
		return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(p_pointer));
	}

	uint64_t HashFloat(float p_value)
	{
		uint32_t bits = 0;
		std::memcpy(&bits, &p_value, sizeof(bits));
		return static_cast<uint64_t>(bits);
	}

	void HashMatrix(uint64_t& p_seed, const Maths::FMatrix4& p_matrix)
	{
		for (int i = 0; i < 16; ++i)
		{
			HashCombine(p_seed, HashFloat(p_matrix.data[i]));
		}
	}

	void HashSphere(uint64_t& p_seed, const ::Rendering::Geometry::BoundingSphere& p_sphere)
	{
		HashCombine(p_seed, HashFloat(p_sphere.position.x));
		HashCombine(p_seed, HashFloat(p_sphere.position.y));
		HashCombine(p_seed, HashFloat(p_sphere.position.z));
		HashCombine(p_seed, HashFloat(p_sphere.radius));
	}

	class SceneRenderPass : public Rendering::Core::ARenderPass
	{
	public:
		SceneRenderPass(Rendering::Core::CompositeRenderer& p_renderer, bool stencilWrite = false) :
			Rendering::Core::ARenderPass(p_renderer),
			m_stencilWrite(stencilWrite)
		{
		}

	protected:
		void PrepareStencilBuffer(Rendering::Data::PipelineState& p_pso)
		{
			p_pso.stencilTest = true;
			p_pso.stencilWriteMask = 0xFF;
			p_pso.stencilFuncRef = 1;
			p_pso.stencilFuncMask = 0xFF;
			p_pso.stencilOpFail = Rendering::Settings::EOperation::REPLACE;
			p_pso.depthOpFail = Rendering::Settings::EOperation::REPLACE;
			p_pso.bothOpFail = Rendering::Settings::EOperation::REPLACE;
			p_pso.colorWriting.mask = 0x00;
		}

	private:
		bool m_stencilWrite;
	};

	class OpaqueRenderPass : public SceneRenderPass
	{
	public:
		OpaqueRenderPass(Rendering::Core::CompositeRenderer& p_renderer, bool p_stencilWrite = false) :
			SceneRenderPass(p_renderer, p_stencilWrite)
		{
		}

	protected:
		virtual void Draw(Rendering::Data::PipelineState p_pso) override
		{
			ZoneScoped;
			TracyGpuZone("OpaqueRenderPass");

			PrepareStencilBuffer(p_pso);

			const auto& drawables = m_renderer.GetDescriptor<SceneRenderer::SceneFilteredDrawablesDescriptor>();

			for (const auto& drawable : drawables.opaques | std::views::values)
			{
				m_renderer.DrawEntity(p_pso, drawable);
			}
		}
	};
	class LineRenderPass : public SceneRenderPass
	{
	public:
		LineRenderPass(Rendering::Core::CompositeRenderer& p_renderer, bool p_stencilWrite = false) :
			SceneRenderPass(p_renderer, p_stencilWrite)
		{
		}

	protected:
		virtual void Draw(Rendering::Data::PipelineState p_pso) override
		{
			ZoneScoped;
			TracyGpuZone("LineRenderPass");

			PrepareStencilBuffer(p_pso);

			// Mesh edge lines are coplanar with the faces they outline. With the
			// default strict LESS depth test they fail at equal depth, so the faces
			// hide the lines. The line pass runs after the opaque pass, so use
			// LEQUAL and skip depth writes: coplanar lines win, and lines never
			// occlude anything behind them.
			p_pso.depthFunc = ::Rendering::Settings::EComparaisonAlgorithm::GREATER_EQUAL;

			// Not every drawable of this pass is a line: the topology vertices are
			// point sprites, and their size comes from the shader (gl_PointSize),
			// which the driver only honours while this is enabled.
			glEnable(GL_PROGRAM_POINT_SIZE);

			const auto& drawables = m_renderer.GetDescriptor<SceneRenderer::SceneFilteredDrawablesDescriptor>();

			for (auto drawable : drawables.lines | std::views::values)
			{
				drawable.stateMask.depthWriting = false;
				m_renderer.DrawEntity(p_pso, drawable);
			}
		}
	};
	class SectionCapRenderPass : public SceneRenderPass
	{
	public:
		SectionCapRenderPass(Rendering::Core::CompositeRenderer& p_renderer) :
			SceneRenderPass(p_renderer)
		{
			m_capMaterial.SetShader(GetShaderService[":Shaders\\SectionCap.ovfx"]);
			m_capMaterial.SetBackfaceCulling(false);
			m_capMaterial.SetFrontfaceCulling(false);
			m_capMaterial.SetDepthWriting(true);
			m_capMaterial.SetDepthTest(true);


			m_parityMaterial.SetShader(GetShaderService[":Shaders\\SectionParity.ovfx"]);
			m_parityMaterial.SetBackfaceCulling(false);
			m_parityMaterial.SetFrontfaceCulling(false);
			m_parityMaterial.SetDepthWriting(false);
			m_parityMaterial.SetDepthTest(false);
			m_parityMaterial.SetColorWriting(false);
		}

	protected:
		virtual void Draw(Rendering::Data::PipelineState p_pso) override
		{
			auto& feature = m_renderer.GetFeature<::Core::Rendering::EngineBufferRenderFeature>();
			if (feature.IsEnableClip()) {
				ZoneScoped;
				TracyGpuZone("SectionCapRenderPass");

				const auto& frameDescriptor = m_renderer.GetFrameDescriptor();
				if (!frameDescriptor.outputMsaaBuffer) return;

				// Stencil parity fill (same idea as OCCT capping): clear the stencil,
				// then INVERT it for every model surface closer to the eye than the
				// clip plane. Odd parity means the plane cuts the model at this pixel.
				m_renderer.Clear(false, false, true, Maths::FVector4(0.0f, 0.0f, 0.0f, 0.0f));

				p_pso.stencilTest = true;
				p_pso.stencilWriteMask = 0xFF;
				p_pso.stencilFuncOp = ::Rendering::Settings::EComparaisonAlgorithm::ALWAYS;
				p_pso.stencilFuncRef = 0;
				p_pso.stencilFuncMask = 0xFF;
				p_pso.stencilOpFail = ::Rendering::Settings::EOperation::KEEP;
				p_pso.depthOpFail = ::Rendering::Settings::EOperation::KEEP;
				p_pso.bothOpFail = ::Rendering::Settings::EOperation::INVERT;
				p_pso.colorWriting.mask = 0x00;
				p_pso.depthWriting = false;
				p_pso.depthTest = false;

				const auto& drawables = m_renderer.GetDescriptor<SceneRenderer::SceneFilteredDrawablesDescriptor>();
				for (auto drawable : drawables.opaques | std::views::values)
				{
					if (drawable.primitiveMode != ::Rendering::Settings::EPrimitiveMode::TRIANGLES) continue;
					const auto& desc = drawable.GetDescriptor<SceneRenderer::SceneDrawableDescriptor>();
					if (desc.actor.GetTag() == "SkyBox") continue;

					drawable.pass = "";
					drawable.material = m_parityMaterial;
					drawable.stateMask = m_parityMaterial.GenerateStateMask();
					drawable.stateMask.depthTest = false;
					drawable.stateMask.depthWriting = false;
					drawable.stateMask.colorWriting = false;
					drawable.stateMask.frontfaceCulling = false;
					drawable.stateMask.backfaceCulling = false;
					m_renderer.DrawEntity(p_pso, drawable);
				}

				// Draw the cap where the parity is odd, occluded by kept surfaces in
				// front of the clip plane (depth test against the scene depth). The
				// IBL textures are resolved here because the skybox pass allocates
				// them on its first draw, after this pass is constructed.
				m_capMaterial.SetProperty("_IrradianceCube", m_renderer.GetIrradianceCube());
				m_capMaterial.SetProperty("_PrefilterCube", m_renderer.GetPrefilterCube());
				m_capMaterial.SetProperty("_BRDFLut", m_renderer.GetBrdfTexture());
				p_pso.stencilFuncOp = ::Rendering::Settings::EComparaisonAlgorithm::NOTEQUAL;
				p_pso.stencilFuncRef = 0;
				p_pso.stencilFuncMask = 0xFF;
				p_pso.stencilOpFail = ::Rendering::Settings::EOperation::KEEP;
				p_pso.depthOpFail = ::Rendering::Settings::EOperation::KEEP;
				p_pso.bothOpFail = ::Rendering::Settings::EOperation::KEEP;
				p_pso.stencilWriteMask = 0x00;
				p_pso.colorWriting.mask = 0xFF;
				p_pso.depthWriting = false;
				p_pso.depthTest = true;
				p_pso.depthFunc = ::Rendering::Settings::EComparaisonAlgorithm::GREATER_EQUAL;

				Rendering::Entities::Drawable drawable;
				drawable.mesh = m_renderer.m_unitQuad;
				drawable.material = m_capMaterial;
				drawable.stateMask = m_capMaterial.GenerateStateMask();
				drawable.stateMask.depthWriting = true;
				drawable.stateMask.frontfaceCulling = false;
				drawable.stateMask.backfaceCulling = false;
				m_renderer.DrawEntity(p_pso, drawable);

			}
		}

	private:
		Rendering::Data::Material m_capMaterial;
		Rendering::Data::Material m_parityMaterial;
	};
	class SectionContourRenderPass : public SceneRenderPass
	{
	public:
		SectionContourRenderPass(Rendering::Core::CompositeRenderer& p_renderer) :
			SceneRenderPass(p_renderer)
		{
			m_contourMaterial.SetShader(GetShaderService[":Shaders\\SectionContour.ovfx"]);
			m_contourMaterial.SetBackfaceCulling(false);
			m_contourMaterial.SetFrontfaceCulling(false);
			m_contourMaterial.SetDepthWriting(false);
			m_contourMaterial.SetDepthTest(true);
			m_contourMaterial.SetLineWidth(3.0);
			m_contourMaterial.SetProperty("color", Maths::FVector3(1.0f, 0.0f, 1.0f));
		}

	protected:
		virtual void Draw(Rendering::Data::PipelineState p_pso) override
		{
			auto& feature = m_renderer.GetFeature<::Core::Rendering::EngineBufferRenderFeature>();
			if (feature.IsEnableClip()) {
			
				ZoneScoped;
				TracyGpuZone("SectionContourRenderPass");

				PrepareStencilBuffer(p_pso);

				p_pso.depthFunc = ::Rendering::Settings::EComparaisonAlgorithm::GREATER_EQUAL;

				const auto& drawables = m_renderer.GetDescriptor<SceneRenderer::SceneFilteredDrawablesDescriptor>();
				for (auto drawable : drawables.opaques | std::views::values)
				{
					if (drawable.primitiveMode != ::Rendering::Settings::EPrimitiveMode::TRIANGLES) continue;
					const auto& desc = drawable.GetDescriptor<SceneRenderer::SceneDrawableDescriptor>();
					if (desc.actor.GetTag() == "SkyBox") continue;

					drawable.pass = "";
					drawable.material = m_contourMaterial;
					drawable.stateMask = m_contourMaterial.GenerateStateMask();
					m_renderer.DrawEntity(p_pso, drawable);
				}			
			}
		}
	private:
		Rendering::Data::Material m_contourMaterial;
	};
	class TransparentRenderPass : public SceneRenderPass
	{
	public:
		TransparentRenderPass(Rendering::Core::CompositeRenderer& p_renderer, bool p_stencilWrite = false) :
			SceneRenderPass(p_renderer, p_stencilWrite) {
			mLayerFbo.resize(8);
			::Rendering::Settings::TextureDesc desc;
			desc.internalFormat = ::Rendering::Settings::EInternalFormat::RGBA32F;
			desc.width = 1;
			desc.height = 1;
			desc.minFilter = ::Rendering::Settings::ETextureFilteringMode::LINEAR;
			desc.magFilter = ::Rendering::Settings::ETextureFilteringMode::LINEAR;
		
			desc.mutableDesc = ::Rendering::Settings::MutableTextureDesc{
				.format = ::Rendering::Settings::EFormat::RGBA,
				.type = ::Rendering::Settings::EPixelDataType::FLOAT,
				.arrayLayers= static_cast<int>(mLayerFbo.size())
			    
			};
			mLayerColor = std::make_shared<::Rendering::HAL::Texture>(
				::Rendering::Settings::ETextureType::TEXTURE_2DARRAY, "layercolor");
			mLayerColor->Allocate(desc);
			

			
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
			::Rendering::Settings::TextureDesc depthDesc{
				.width = 1,
				.height = 1,
				.minFilter = ::Rendering::Settings::ETextureFilteringMode::NEAREST,
				.magFilter = ::Rendering::Settings::ETextureFilteringMode::NEAREST,
				.horizontalWrap = ::Rendering::Settings::ETextureWrapMode::REPEAT,
				.verticalWrap = ::Rendering::Settings::ETextureWrapMode::REPEAT,
				.internalFormat = ::Rendering::Settings::EInternalFormat::DEPTH_COMPONENT32F,
				.useMipMaps = false,
				.mutableDesc = ::Rendering::Settings::MutableTextureDesc{
					.format = ::Rendering::Settings::EFormat::DEPTH_COMPONENT,
					.type = ::Rendering::Settings::EPixelDataType::FLOAT
				}
			};
			for (int i = 0; i < mLayerFbo.size(); i++) {
				auto renderTexture = std::make_shared<::Rendering::HAL::Texture>(
					 ::Rendering::Settings::ETextureType::TEXTURE_2D,"peelcolor");
				renderTexture->Allocate(colorDesc);
				auto depthTexture = std::make_shared<::Rendering::HAL::Texture>(
					::Rendering::Settings::ETextureType::TEXTURE_2D,"peeldepth");
				depthTexture->Allocate(depthDesc);
				depthTexture->SetBorderColor(Maths::FVector4::Zero);
				mLayerFbo[i].Attach<::Rendering::HAL::Texture>(renderTexture, ::Rendering::Settings::EFramebufferAttachment::COLOR);
				mLayerFbo[i].Attach<::Rendering::HAL::Texture>(depthTexture, ::Rendering::Settings::EFramebufferAttachment::DEPTH);
				if (!mLayerFbo[i].Validate()) {
					std::cout << "invalidate buffer" << std::endl;
				}
			}

			auto renderTexture = std::make_shared<::Rendering::HAL::Texture>(
				::Rendering::Settings::ETextureType::TEXTURE_2D, "peelcolor");
			renderTexture->Allocate(colorDesc);
			auto depthTexture = std::make_shared<::Rendering::HAL::Texture>(
				::Rendering::Settings::ETextureType::TEXTURE_2D, "peeldepth");
			depthTexture->Allocate(depthDesc);
			depthTexture->SetBorderColor(Maths::FVector4::Zero);
			mBlendFbo.Attach<::Rendering::HAL::Texture>(renderTexture, ::Rendering::Settings::EFramebufferAttachment::COLOR);
			mBlendFbo.Attach<::Rendering::HAL::Texture>(depthTexture, ::Rendering::Settings::EFramebufferAttachment::DEPTH);
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

uniform sampler2D colorLayer;

void main()
{
	FRAGMENT_COLOR = texture(colorLayer, TexCoords);
}

)";
			mBlendColorShader = ::Rendering::Resources::Loaders::ShaderLoader::CreateFromSource(v, f);
			mBlendColorMat.SetShader(mBlendColorShader);
		}

	protected:
		virtual void ResizeRenderer(int width, int height) override {
			mLayerColor->Resize(width,height);
			mBlendFbo.Resize(width,height);
			for (int i = 0; i < mLayerFbo.size(); i++) {
				mLayerFbo[i].Resize(width,height);
			}
		}
		virtual void Draw(Rendering::Data::PipelineState p_pso) override
		{
			ZoneScoped;
			TracyGpuZone("TransparentRenderPass");
			const auto& drawables = m_renderer.GetDescriptor<SceneRenderer::SceneFilteredDrawablesDescriptor>();
			if (drawables.transparents.size()>0) {
				auto& msaaframebuffer = m_renderer.GetFrameDescriptor().outputMsaaBuffer.value();
				Core::Rendering::FramebufferUtil::CopyFramebufferColor(msaaframebuffer, 0, mBlendFbo, 0);
				Core::Rendering::FramebufferUtil::CopyFramebufferDepth(msaaframebuffer, mBlendFbo); 
			
				//peel 0 layers, use the depth buffer copied from opaque pass to peel the first layer of transparent
				//we can have better method to optimize the copy like that
				//we prepare a empty(value of depth is 0) depth texture,then we can unify the shader and (step1 and step2) for peeling all layers,
				//but for now we just want to get it working
				Core::Rendering::FramebufferUtil::CopyFramebufferDepth(mBlendFbo, mLayerFbo[0]);
				PrepareStencilBuffer(p_pso);
				/*
				1.the first step: peel the first layer of transparent and write the depth to layerfbo0, then we can use this depth to peel the second layer and so on
				*/
				mLayerFbo[0].Bind();
				m_renderer.Clear(true,false,false,Maths::FVector4(0,0,0,0));

				for ( auto drawable : drawables.transparents | std::views::values)
				{
					drawable.pass = "";				
					drawable.stateMask.depthWriting = true;
					drawable.stateMask.colorWriting = true;
					drawable.stateMask.blendable = false;
					drawable.stateMask.frontfaceCulling = false;
					drawable.stateMask.backfaceCulling = false;
					drawable.stateMask.depthTest = true;
					m_renderer.DrawEntity(p_pso, drawable);
				}
				mLayerFbo[0].Unbind();
				/*
				* 2.the second step: peel the second layer and the others, 
				we use the depth texture of previous layer to peel the next layer, 
				and we also need the depth texture of opaque layer to do culling,
				so we need to bind both of them to shader, 
				then we can peel all the layers in one pass,
				but for now we just do it one by one
				*/
				//peel the others layers
				for (int layer = 1; layer < mLayerFbo.size(); layer++) {
			
					int currFbo = layer ;
					int prevFbo = (layer - 1) ;

					// 绑定当前 FBO
					mLayerFbo[currFbo].Bind();
					m_renderer.Clear(true, true, false, Maths::FVector4(0, 0, 0, 0));
					const auto& depth=mLayerFbo[prevFbo].GetAttachment<::Rendering::HAL::GLTexture>(::Rendering::Settings::EFramebufferAttachment::DEPTH,0);
					const auto& opaquedepth = mBlendFbo.GetAttachment<::Rendering::HAL::GLTexture>(::Rendering::Settings::EFramebufferAttachment::DEPTH, 0);
					for ( auto drawable : drawables.transparents | std::views::values)
					{
						drawable.stateMask.depthWriting = true;
						drawable.stateMask.colorWriting = true;
						drawable.stateMask.blendable = false;
						drawable.stateMask.frontfaceCulling = false;
						drawable.stateMask.backfaceCulling = false;
						drawable.stateMask.depthTest = true;
						drawable.material->TrySetProperty("DepthPeelTex",&depth.value());
						drawable.material->TrySetProperty("OpaqueDepthTex", &opaquedepth.value());
						m_renderer.DrawEntity(p_pso, drawable);
					}
					mLayerFbo[currFbo].Unbind();
				}
				/*
				 * 3.the third step: blend the peeled layers back to front,
				 * we can also do it in one pass by binding all the peeled color textures to shader, but for now we just do it one by one
				 */
				mBlendFbo.Bind();
				for (int i = mLayerFbo.size() - 1; i >= 0; i--) {
					const auto& color = mLayerFbo[i].GetAttachment<::Rendering::HAL::GLTexture>(::Rendering::Settings::EFramebufferAttachment::COLOR, 0);
					mBlendColorMat.SetProperty("colorLayer",&color.value());
					Rendering::Entities::Drawable blit;
					blit.mesh = m_renderer.m_unitQuad;
					blit.material = mBlendColorMat;
					blit.stateMask.depthWriting = false;
					blit.stateMask.colorWriting = true;
					blit.stateMask.blendable = true;
					blit.stateMask.frontfaceCulling = false;
					blit.stateMask.backfaceCulling = false;
					blit.stateMask.depthTest = false;
					m_renderer.DrawEntity(p_pso,blit);
				}
				mBlendFbo.Unbind();
				
				msaaframebuffer.Bind();
				const auto& content = mBlendFbo.GetAttachment<::Rendering::HAL::GLTexture>(::Rendering::Settings::EFramebufferAttachment::COLOR, 0);
				m_renderer.Present(content.value());
			}
		}
	private:
		::Rendering::Resources::Shader* mBlendColorShader;
		::Rendering::Data::Material mBlendColorMat;
		std::vector<::Rendering::HAL::Framebuffer>mLayerFbo ;
		::Rendering::HAL::Framebuffer mBlendFbo;
		std::shared_ptr<::Rendering::HAL::Texture>mLayerColor;
	};

	class UIRenderPass : public SceneRenderPass
	{
	public:
		UIRenderPass(Rendering::Core::CompositeRenderer& p_renderer, bool p_stencilWrite = false) :
			SceneRenderPass(p_renderer, p_stencilWrite) {
		}

	protected:
		virtual void Draw(Rendering::Data::PipelineState p_pso) override
		{
			ZoneScoped;
			TracyGpuZone("UIRenderPass");

			PrepareStencilBuffer(p_pso);

			const auto& drawables = m_renderer.GetDescriptor<SceneRenderer::SceneFilteredDrawablesDescriptor>();

			for (const auto& drawable : drawables.ui | std::views::values)
			{
				m_renderer.DrawEntity(p_pso, drawable);
			}
		}
	};

	Rendering::Features::LightingRenderFeature::LightSet FindActiveLights(const Core::SceneSystem::Scene& p_scene)
	{
		Rendering::Features::LightingRenderFeature::LightSet lights;

		const auto& facs = p_scene.GetFastAccessComponents();

		for (auto light : facs.lights)
		{
			if (light->owner.IsActive())
			{
				lights.push_back(std::ref(light->GetData()));
			}
		}

		return lights;
	}

	std::vector<std::reference_wrapper<Core::ECS::Components::CReflectionProbe>> FindActiveReflectionProbes(const Core::SceneSystem::Scene& p_scene)
	{
		std::vector<std::reference_wrapper<Core::ECS::Components::CReflectionProbe>> probes;
		const auto& facs = p_scene.GetFastAccessComponents();
		for (auto probe : facs.reflectionProbes)
		{
			if (probe->owner.IsActive())
			{
				probes.push_back(*probe);
			}
		}
		return probes;
	}
}

Core::Rendering::SceneRenderer::SceneRenderer(::Rendering::Context::Driver& p_driver, bool p_stencilWrite)
	: ::Rendering::Core::CompositeRenderer(p_driver)
{
	using namespace ::Rendering::Features;
	using namespace ::Rendering::Settings;
	using enum ::Rendering::Features::EFeatureExecutionPolicy;

	AddFeature<EngineBufferRenderFeature, ALWAYS>();
	AddFeature<LightingRenderFeature, ALWAYS>();

	//AddFeature<ReflectionRenderFeature, WHITELIST_ONLY>()
	//	.Include<OpaqueRenderPass>()
	//	.Include<TransparentRenderPass>();
	//AddFeature<SsaoRenderFeature, WHITELIST_ONLY>()
	//	.Include<OpaqueRenderPass>();

	//AddFeature<ShadowRenderFeature, WHITELIST_ONLY>()
	//	.Include<OpaqueRenderPass>()
	//	.Include<TransparentRenderPass>()
	//	.Include<UIRenderPass>();

	//AddPass<ShadowRenderPass>("Shadows", ERenderPassOrder::Shadows);
	//AddPass<ReflectionRenderPass>("ReflectionRenderPass", ERenderPassOrder::Reflections);
	AddPass<SkyboxRenderPass>("SkyboxRenderPass",ERenderPassOrder::SkyBox);
	AddPass<GbufferPass>("Gbuffer", ERenderPassOrder::Opaque-1);
	AddPass<OpaqueRenderPass>("Opaques", ERenderPassOrder::Opaque, p_stencilWrite);

	AddPass<SectionCapRenderPass>("SectionCap", ERenderPassOrder::SectionCap);
	AddPass<SectionContourRenderPass>("SectionContour", ERenderPassOrder::SectionContour);


	AddPass<LineRenderPass>("Lines", ERenderPassOrder::LineAfterPathTrace, p_stencilWrite);
	AddPass<TransparentRenderPass>("Transparents", ERenderPassOrder::Transparent, p_stencilWrite);
	{
		// Builds the max depth pyramid from the opaque depth and feeds the
		// CPU side BVH occlusion culler used by the next frame.
		auto& hzbPass = AddPass<HzbBuildPass>("HZB", ERenderPassOrder::HzbBuild);
		hzbPass.SetCuller(&m_hzbCuller);
	}
	AddPass<PostProcessRenderPass>("Post-Process", ERenderPassOrder::PostProcessing);
	//AddPass<UIRenderPass>("UI", ERenderPassOrder::UI);
}

void Core::Rendering::SceneRenderer::BeginFrame(const ::Rendering::Data::FrameDescriptor& p_frameDescriptor)
{
	ZoneScoped;

	assert(HasDescriptor<SceneDescriptor>()&& "Cannot find SceneDescriptor attached to this renderer");

	auto& sceneDescriptor = GetDescriptor<SceneDescriptor>();

	const bool frustumLightCulling = p_frameDescriptor.camera.value().HasFrustumLightCulling();

	AddDescriptor<::Rendering::Features::LightingRenderFeature::LightingDescriptor>({
		FindActiveLights(sceneDescriptor.scene),
		frustumLightCulling ? sceneDescriptor.frustumerride : std::nullopt
		});

	AddDescriptor<Core::Rendering::ReflectionRenderFeature::ReflectionDescriptor>({
		FindActiveReflectionProbes(sceneDescriptor.scene)
		});

	::Rendering::Core::CompositeRenderer::BeginFrame(p_frameDescriptor);

	// Parsed drawables are cached inside the renderer; parsing only runs when the
	// scene content actually changed. The per frame descriptor is a pointer to
	// that cache, so a hit costs neither copies nor allocations.
	UpdateParsedDrawables(sceneDescriptor.scene);
	AddDescriptor<SceneDrawablesHandle>({ &m_parsedDrawables });

	// Default filtered drawables descriptor using the main camera (used by most render passes).
	// Some other render passes can decide to filter the drawables themselves, using the
	// SceneDrawablesHandle instead of the SceneFilteredDrawablesDescriptor one.
	AddDescriptor<SceneFilteredDrawablesDescriptor>({
		FilterDrawables(
			*GetDescriptor<SceneDrawablesHandle>().drawables,
			SceneDrawablesFilteringInput{
				.camera = p_frameDescriptor.camera.value(),
				.frustumerride = sceneDescriptor.frustumerride,
				.errideMaterial = sceneDescriptor.errideMaterial,
				.fallbackMaterial = sceneDescriptor.fallbackMaterial,
				.requiredVisibilityFlags = EVisibilityFlags::GEOMETRY
			}
		)
		});
}
void Core::Rendering::SceneRenderer::Resize(int width, int height)
{
	for (const auto& pass : m_passes | std::views::values)
	{
		//if (pass.second->IsEnabled())
		{
			pass.second->ResizeRenderer(width,height);;
		}
	}
}
void Core::Rendering::SceneRenderer::DrawModelWithSingleMaterial(::Rendering::Data::PipelineState p_pso, ::Rendering::Resources::Model& p_model, ::Rendering::Data::Material& p_material, const Maths::FMatrix4& p_modelMatrix)
{
	auto stateMask = p_material.GenerateStateMask();
	auto userMatrix = Maths::FMatrix4::Identity;

	auto engineDrawableDescriptor = EngineDrawableDescriptor{
		p_modelMatrix,
		userMatrix
	};

	for (auto mesh : p_model.GetMeshes())
	{
		::Rendering::Entities::Drawable element;
		element.mesh = *mesh;
		element.material = p_material;
		element.stateMask = stateMask;
		element.AddDescriptor(engineDrawableDescriptor);

		DrawEntity(p_pso, element);
	}
}

SceneRenderer::SceneDrawablesDescriptor Core::Rendering::SceneRenderer::ParseScene(const SceneParsingInput& p_input)
{
	ZoneScoped;

	using namespace Core::ECS::Components;

	// Containers for the parsed drawables. The previous frame's count is used as
	// a reserve hint so the vector does not have to grow (and relocate every
	// Drawable, each of which owns heap backed descriptor storage) while filling.
	SceneRenderer::SceneDrawablesDescriptor result;
	result.drawables.reserve(m_lastParsedDrawableCount);

	const auto& scene = p_input.scene;

	for (const auto modelRenderer : scene.GetFastAccessComponents().modelRenderers)
	{
		auto& owner = modelRenderer->owner;
		if (!owner.IsActive()) continue;
		const auto model = modelRenderer->GetModel();
		if (!model) continue;
		const auto materialRenderer = modelRenderer->owner.GetComponent<CMaterialRenderer>();
		if (!materialRenderer) continue;

		const auto& transform = owner.transform.GetFTransform();
		const auto& materials = materialRenderer->GetMaterials();

		for (auto& mesh : model->GetMeshes())
		{
			// Read by reference: these getters used to return copies, which cost
			// two heap allocations per mesh per frame.
			const auto& meshMatIndex = mesh->GetMaterialIndex();
			const auto& meshRangeBufferIndex = mesh->GetSubRangeBufferIndex();
			for(int i=0;i<meshMatIndex.size();i++)
			{
				const auto materialIndex = meshMatIndex[i];
				int bufferIndex = meshRangeBufferIndex[i];
				if (mesh->GetIndexCount(bufferIndex) <= 0) {
					continue;
				}
				Tools::Utils::OptRef<::Rendering::Data::Material> material;

				if (materialIndex < kMaxMaterialCount)
				{
					material = materials.at(materialIndex);
				}
				auto primMode=mesh->GetPrimitiveMode();
				::Rendering::Entities::Drawable drawable{
					.mesh = *mesh,
					.material = material,
					.stateMask = material.has_value() ? material->GenerateStateMask() : ::Rendering::Data::StateMask{},
					.subIndexBufferIndex= bufferIndex,
					.primitiveMode=primMode,
				};

				auto bounds = [&]() -> std::optional<::Rendering::Geometry::BoundingSphere> {
					using enum CModelRenderer::EFrustumBehaviour;
					switch (modelRenderer->GetFrustumBehaviour())
					{
					case MESH_BOUNDS: return mesh->GetBoundingSphere();
					case DEPRECATED_MODEL_BOUNDS: return model->GetBoundingSphere();
					case CUSTOM_BOUNDS: return modelRenderer->GetCustomBoundingSphere();
					}
					return std::nullopt;
					}();
				drawable.AddDescriptor<SceneDrawableDescriptor>({
					.actor = modelRenderer->owner,
					.visibilityFlags = materialRenderer->GetVisibilityFlags(),
					.bounds = bounds,
					.sourceMesh = mesh,
					});
				drawable.AddDescriptor<EngineDrawableDescriptor>({
					transform.GetWorldMatrix(),
					materialRenderer->GetUserMatrix()
					});
				// Move instead of copy: copying a Drawable deep copies its two
				// descriptors (each a map node + a std::any payload).
				result.drawables.push_back(std::move(drawable));
			}
		}
	}

	m_lastParsedDrawableCount = result.drawables.size();
	return result;
}

void Core::Rendering::SceneRenderer::UpdateParsedDrawables(Core::SceneSystem::Scene& p_scene)
{
	ZoneScoped;

	using namespace Core::ECS::Components;

	const auto& renderers = p_scene.GetFastAccessComponents().modelRenderers;

	// Signature over everything ParseScene reads. It avoids the per Drawable
	// allocations, so on an unchanged scene the whole parse step is skipped.
	uint64_t signature = 0xcbf29ce484222325ULL;
	HashCombine(signature, renderers.size());

	for (const auto modelRenderer : renderers)
	{
		const auto& owner = modelRenderer->owner;
		const auto model = modelRenderer->GetModel();
		const auto materialRenderer = owner.GetComponent<CMaterialRenderer>();

		HashCombine(signature, HashPointer(modelRenderer));
		HashCombine(signature, static_cast<uint64_t>(owner.GetID()));
		HashCombine(signature, owner.IsActive() ? 1ULL : 0ULL);
		HashCombine(signature, HashPointer(model));
		HashCombine(signature, HashPointer(materialRenderer));

		if (!owner.IsActive() || model == nullptr || materialRenderer == nullptr)
		{
			continue;
		}

		HashMatrix(signature, owner.transform.GetFTransform().GetWorldMatrix());
		HashMatrix(signature, materialRenderer->GetUserMatrix());
		HashCombine(signature, static_cast<uint64_t>(materialRenderer->GetVisibilityFlags()));

		const auto frustumBehaviour = modelRenderer->GetFrustumBehaviour();
		HashCombine(signature, static_cast<uint64_t>(frustumBehaviour));
		if (frustumBehaviour == CModelRenderer::EFrustumBehaviour::CUSTOM_BOUNDS)
		{
			HashSphere(signature, modelRenderer->GetCustomBoundingSphere());
		}

		const auto& materials = materialRenderer->GetMaterials();
		HashCombine(signature, materials.size());
		for (const auto material : materials)
		{
			HashCombine(signature, HashPointer(material));
		}

		for (const auto mesh : model->GetMeshes())
		{
			HashCombine(signature, HashPointer(mesh));
			if (mesh == nullptr)
			{
				continue;
			}

			const auto& materialIndices = mesh->GetMaterialIndex();
			const auto& rangeIndices = mesh->GetSubRangeBufferIndex();
			HashCombine(signature, materialIndices.size());
			HashCombine(signature, rangeIndices.size());
			for (const auto materialIndex : materialIndices)
			{
				HashCombine(signature, materialIndex);
			}
			for (const auto rangeIndex : rangeIndices)
			{
				HashCombine(signature, rangeIndex);
			}
			HashCombine(signature, static_cast<uint64_t>(mesh->GetPrimitiveMode()));

			const size_t rangeCount = std::min(materialIndices.size(), rangeIndices.size());
			for (size_t i = 0; i < rangeCount; ++i)
			{
				HashCombine(signature, static_cast<uint64_t>(mesh->GetIndexCount(rangeIndices[i])));
			}

			// Bounds stored in the drawable, depending on the frustum behaviour.
			if (frustumBehaviour == CModelRenderer::EFrustumBehaviour::MESH_BOUNDS)
			{
				HashSphere(signature, mesh->GetBoundingSphere());
			}
			else if (frustumBehaviour == CModelRenderer::EFrustumBehaviour::DEPRECATED_MODEL_BOUNDS)
			{
				HashSphere(signature, model->GetBoundingSphere());
			}
		}
	}

	if (m_parsedDrawablesValid && signature == m_parsedDrawablesHash)
	{
		return; // Scene unchanged: reuse the cached drawables.
	}

	m_parsedDrawables = ParseScene(SceneParsingInput{ .scene = p_scene });
	m_parsedDrawablesHash = signature;
	m_parsedDrawablesValid = true;
}

SceneRenderer::SceneFilteredDrawablesDescriptor Core::Rendering::SceneRenderer::FilterDrawables(
	const SceneDrawablesDescriptor& p_drawables,
	const SceneDrawablesFilteringInput& p_filteringInput
)
{
	ZoneScoped;

	using namespace Core::ECS::Components;

	SceneFilteredDrawablesDescriptor output;

	const auto& camera = p_filteringInput.camera;
	const auto& frustumerride = p_filteringInput.frustumerride;

	// Determine if we should use frustum culling
	Tools::Utils::OptRef<const ::Rendering::Data::Frustum> frustum;
	if (camera.HasFrustumGeometryCulling())
	{
		frustum = frustumerride ? frustumerride : camera.GetFrustum();
	}

	// Hierarchical Z-buffer occlusion culling. The depth grid is produced by the
	// HZB pass from the previous frame; the pass also owns the bias parameter
	// exposed in the pass settings. When the pass is disabled no culling happens
	// and the previous frame's results are dropped.
	auto& hzbPass = GetPass<::Core::Rendering::HzbBuildPass>("HZB");
	m_hzbCuller.SetDepthBias(hzbPass.GetDepthBias());
	m_hzbCuller.SetStaticDepthBias(hzbPass.GetStaticDepthBias());
	bool isEnableHzb = hzbPass.IsEnabled();

	auto& sceneDescriptor = GetDescriptor<SceneRenderer::SceneDescriptor>();
	if (!isEnableHzb)
	{
		m_hzbCuller.ClearGrid();
	}
	else if (auto* bvhService = sceneDescriptor.scene.GetBvhService())
	{
		if (bvhService->m_sceneBvh != nullptr && bvhService->m_sceneBvh->m_root != nullptr)
		{
			ZoneScopedN("HZB Culling");

			m_hzbCuller.Cull(
				*bvhService,
				camera.GetViewProjectionMatrix(),
				m_frameDescriptor.renderWidth,
				m_frameDescriptor.renderHeight
			);
		}
	}

	m_hzbSkippedDrawables = 0;

	// Process each drawable
	for (const auto& drawable : p_drawables.drawables)
	{
		const auto& desc = drawable.GetDescriptor<SceneDrawableDescriptor>();

		// Skip drawables that do not satisfy the required visibility flags
		if (!SatisfiesVisibility(desc.visibilityFlags, p_filteringInput.requiredVisibilityFlags))
		{
			continue;
		}

		const auto targetMaterial =
			p_filteringInput.errideMaterial.has_value() ?
			p_filteringInput.errideMaterial.value() :
			(drawable.material.has_value() ? drawable.material.value() : p_filteringInput.fallbackMaterial);

		// Skip if material is invalid
		if (!targetMaterial || !targetMaterial->IsValid()) continue;

		// Filter drawables based on the type (UI, opaque, transparent)
		// Except for the fallback material, which is always included.
		if (!p_filteringInput.fallbackMaterial || &p_filteringInput.fallbackMaterial.value() != &targetMaterial.value())
		{
			const bool isUI = targetMaterial->IsUserInterface();
			if (isUI && !p_filteringInput.includeUI) continue;
			if (!isUI && !targetMaterial->IsBlendable() && !p_filteringInput.includeOpaque) continue;
			if (!isUI && targetMaterial->IsBlendable() && !p_filteringInput.includeTransparent) continue;
		}

		// Perform frustum culling if enabled
		if (frustum && desc.bounds.has_value())
		{
			ZoneScopedN("Frustum Culling");
			if (!frustum->BoundingSphereInFrustum(desc.bounds.value(), desc.actor.transform.GetFTransform()))
			{
				continue; // Skip this drawable as it's outside the frustum
			}
		}

		// Occlusion culling: skip drawables whose source mesh is fully behind the
		// previous frame's depth (hierarchical Z-buffer test on the BVH). The
		// granularity is the mesh instance, which matches the drawable
		// granularity produced by ParseScene (one drawable per mesh sub-range).
		if (isEnableHzb) {
			if (m_hzbCuller.IsOccluded(desc.sourceMesh, desc.actor.GetID()))
			{
				++m_hzbSkippedDrawables;
				continue;
			}
		}

		// Calculate distance to camera for sorting
		const float distanceToCamera = Maths::FVector3::Distance(
			desc.actor.transform.GetWorldPosition(),
			camera.GetPosition()
		);

		// At this point we want to copy the drawable to avoid modifying the original one.
		// The copy will use the updated material.
		// At this point, the filtered drawable should be guaranteed to have a valid material.
		auto drawableCopy = drawable;
		drawableCopy.material = targetMaterial;
		drawableCopy.stateMask = targetMaterial->GenerateStateMask();
		
		// Key fields are read before the drawable is moved into the map, so the
		// argument evaluation order cannot read a moved-from drawable.
		const int drawOrder = targetMaterial->GetDrawOrder();
		const uintptr_t materialKey = reinterpret_cast<uintptr_t>(&targetMaterial.value());

		// Categorize drawable based on their type.
		// This is also where sorting happens, using
		// the multimap key.
		if (drawableCopy.material->IsUserInterface())
		{
			output.ui.emplace(decltype(decltype(output.ui)::value_type::first){
				.order = drawOrder,
					.materialKey = materialKey,
					.distance = distanceToCamera
			}, std::move(drawableCopy));
		}
		else if (drawableCopy.primitiveMode == ::Rendering::Settings::EPrimitiveMode::LINES
			|| drawableCopy.primitiveMode == ::Rendering::Settings::EPrimitiveMode::POINTS) {
			// Edge lines and the vertex dots of a topology shape are the same kind of
			// thing: both lie on the surface they belong to (a line exactly on the
			// faces it borders, a vertex dot on the corner where they meet), so both
			// go through the pass that draws them after the opaque pass and without
			// depth writes - that is what keeps them from losing to those faces.
			output.lines.emplace(decltype(decltype(output.lines)::value_type::first){
				.order = drawOrder,
					.materialKey = materialKey,
					.distance = distanceToCamera
			}, std::move(drawableCopy));
		}
		else if (drawableCopy.material->IsTransparent())
		{
			drawableCopy.pass = "Transparents";
			output.transparents.emplace(decltype(decltype(output.transparents)::value_type::first){
				.order = drawOrder,
					.materialKey = materialKey,
					.distance = distanceToCamera
			}, std::move(drawableCopy));
		}
		else
		{
			output.opaques.emplace(decltype(decltype(output.opaques)::value_type::first){
				.order = drawOrder,
					.materialKey = materialKey,
					.distance = distanceToCamera
			}, std::move(drawableCopy));
		}
	}
	return output;
}
