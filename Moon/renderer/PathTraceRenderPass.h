#pragma once
#include <Rendering/Data/PipelineState.h>
#include <Rendering/Core/ARenderPass.h>
#include <Rendering/Data/Framebuffer.h>
#include <Rendering/Data/Texture.h>
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

		::Rendering::Data::Texture* BVHTex = nullptr;
		::Rendering::Data::Texture* vertexIndicesTex = nullptr;
		::Rendering::Data::Texture* verticesTex = nullptr;
		::Rendering::Data::Texture* normalsTex = nullptr;
		::Rendering::Data::Texture* triangleDomainColorTex = nullptr;
		::Rendering::Data::Texture* meshInfoTex = nullptr;
		::Rendering::Data::Texture* triangleInfoTex = nullptr;

		::Rendering::Data::Texture* materialsTex = nullptr;
		::Rendering::Data::Texture* transformsTex = nullptr;
		::Rendering::Data::Texture* lightsTex = nullptr;
		::Rendering::Data::Texture* textureMapsArrayTex = nullptr;
		::Rendering::Data::Texture* envMapTex = nullptr;
		::Rendering::Data::Texture* envMapCDFTex = nullptr;

		EnvironmentMap* envMap=nullptr;

		::Rendering::Data::Texture* denoisedTexture=nullptr;
		::Rendering::Data::Framebuffer pathTracefbo;
		::Rendering::Data::Framebuffer pathTraceFBOLowRes;
		::Rendering::Data::Framebuffer accumFBO;
		::Rendering::Data::Framebuffer outputFBO[2];
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
