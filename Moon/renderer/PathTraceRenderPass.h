#pragma once
#include <Rendering/Core/ARenderPass.h>
#include <Rendering/HAL/Framebuffer.h>
#include <Rendering/HAL/OpenGL/GLTexture.h>
#include <Rendering/Data/Material.h>
#include <Maths/FVector3.h>
#include "OpenImageDenoise/oidn.hpp"
namespace Rendering {
	namespace Resources {
		class Texture;
		class Mesh;
		class Model;
	}
}
namespace Editor::Rendering
{
	class EnvironmentMap;
	class PathTraceRenderPass : public ::Rendering::Core::ARenderPass
	{
	public:
		PathTraceRenderPass(::Rendering::Core::CompositeRenderer& p_renderer);
		~PathTraceRenderPass();
	    
		void DestoryResource();
		void UpdateGPUMaterial();
		void UpdateGPUDataBuffers();
		void UpdateTriangleInfoBuffer();
		void UpdateFBOs();
		void UpdateShaders();
		virtual void ResizeRenderer(int width, int height)override;	

	protected:
		virtual void Draw(::Rendering::Data::PipelineState p_pso) override;
		void Update();
		void Render();
		void Present();
	private:
		//path trace
		::Rendering::Data::Material pathTraceShader;
		::Rendering::Data::Material pathTraceShaderLowRes;
		::Rendering::Data::Material outputShader;
		::Rendering::Data::Material tonemapShader;
		//line split output to screen
		::Rendering::Data::Material* lineOutputShader = nullptr;
		::Rendering::Data::Material lineOutputMat;

		::Rendering::HAL::GLTexture* BVHTex = nullptr;
		::Rendering::HAL::GLTexture* vertexIndicesTex = nullptr;
		::Rendering::HAL::GLTexture* verticesTex = nullptr;
		::Rendering::HAL::GLTexture* normalsTex = nullptr;
		::Rendering::HAL::GLTexture* triangleDomainColorTex = nullptr;
		::Rendering::HAL::GLTexture* meshInfoTex = nullptr;
		::Rendering::HAL::GLTexture* triangleInfoTex = nullptr;

		::Rendering::HAL::GLTexture* materialsTex = nullptr;
		::Rendering::HAL::GLTexture* transformsTex = nullptr;
		::Rendering::HAL::GLTexture* lightsTex = nullptr;
		::Rendering::HAL::GLTexture* textureMapsArrayTex = nullptr;
		::Rendering::HAL::GLTexture* envMapTex = nullptr;
		::Rendering::HAL::GLTexture* envMapCDFTex = nullptr;

		EnvironmentMap* envMap=nullptr;

		::Rendering::HAL::GLTexture* denoisedTexture=nullptr;
		::Rendering::HAL::Framebuffer pathTracefbo;
		::Rendering::HAL::Framebuffer pathTraceFBOLowRes;
		::Rendering::HAL::Framebuffer accumFBO;
		::Rendering::HAL::Framebuffer outputFBO[2];
		Maths::FVector2 renderSize;
		Maths::FVector2 windowSize;
		Maths::FVector2 tile;
		Maths::FVector2 numTiles;
		Maths::FVector2 invNumTiles;
		int tileWidth;
		int tileHeight;
		int currentBuffer=0;
		int frameCounter;
		int sampleCounter;
		float pixelRatio;

		float* denoiserInputFramePtr=nullptr;
		float* frameOutputPtr=nullptr;
		oidn::DeviceRef m_denoiserDevice;
		oidn::FilterRef m_denoiserFilter;
		Maths::FVector2 m_denoiserSize;
		bool m_denoiserFilterDirty = true;
		bool denoised;
		bool initialized;
		bool refreshFlag = true;
		bool needUpdateShader = false;
	};
}
