#include <Rendering/Overlay/Im3DType.h>
#include <Rendering/Backend/BackendResources.h>
#include <Rendering/Backend/CreateTextureRequest.h>
#include <Rendering/Context/Driver.h>
#include <Rendering/HAL/Common/HALDriverAccess.h>
#include <Rendering/HAL/Common/ResourceOps.h>
#include <Rendering/Overlay/ViewCubeLabels.h>
#include <Rendering/Resources/Loaders/TextureLoader.h>

#include <Maths/MathUtil.h>
#include "Core/Global/ServiceLocator.h"
#include "Core/ResourceManagement/TextureManager.h"
#include "Rendering/Resources/Texture.h"
#include "Rendering/Resources/Mesh.h"
#include "Rendering/Resources/Model.h"
#include <Core/Context.h>
#include <Rendering/Overlay/ScreenLayout.h>
#include <glad/glad.h>
#include <Tools/Utils/PathParser.h>

namespace
{
	/** Creates or re-allocates the overlay's texture buffers (the per-face edge values and the
	 * widget block colours).
	 *
	 * The storage is GL, so it is a command: the descriptor's data pointer stays valid because it
	 * points into the mesh's own vectors, and the drain makes the object exist before the material
	 * that samples it is bound. Without a driver at all there is no other side to run it on and it
	 * is done here (docs/RenderThreadPlan.md §8.72). */
	void EnsureTextureBuffer(
		Rendering::Resources::Texture*& p_texture,
		const char* p_name,
		const Rendering::Settings::TextureDesc& p_desc)
	{
		using namespace Rendering::Settings;

		if (p_texture == nullptr)
		{
			p_texture = new Rendering::Resources::Texture(ETextureType::TEXTURE_BUFFER, p_name);
		}

		// The allocation is one command for the front end, which carries the bytes with it (see
		// Data::Texture::Allocate): the descriptor's pointer into the caller's vector is gone by the
		// time the executor runs. With no driver there is no other side and it is done here.
		p_texture->GetTexture().Allocate(p_desc);
	}
}

namespace MOON {


	void PolygonFace::clear() {
		vertex.clear();
		uv.clear();
	}
	PolygonFace::PolygonFace(){
	}
	PolygonFace::PolygonFace(const Eigen::Vector3f& v0, const Eigen::Vector3f& v1, const Eigen::Vector3f& v2, const Eigen::Vector4<uint8_t>& c)
	{
		color = c;
		addPoint(v0,{0,0});
		addPoint(v1, { 0,0 });
		addPoint(v2, { 0,0 });
		n = (v1 - v0).cross(v2-v0).normalized();
	}
	void PolygonFace::addPoint(const Eigen::Vector3f& v, const Eigen::Vector2f& tex)
	{
		vertex.push_back(v);
		uv.push_back(tex);
	}
	void PolygonFace::addPointArray(const std::vector<Eigen::Vector3f>& v, const std::vector<Eigen::Vector2f>& tex)
	{
		vertex = v;
		uv = tex;
	}

	PolygonFace PolygonFace::transform(const Eigen::Matrix4f& mat, float offsetX, float offsetY)
	{
		PolygonFace res;
		for (int i = 0; i < vertex.size();i++) {
			res.addPoint(MatrixMulPoint(mat, vertex[i]), { uv[i].x() + offsetX,uv[i].y() + offsetY });
		}
		res.n = MatrixMulDir(mat, n);
		return res;
	}
	void PolygonFace::tranformUV(float u, float v)
	{
		for (int i = 0; i < vertex.size(); i++) {
			uv[i].x() += u;
			uv[i].y() += v;
		}
	}
	void PolygonMesh::setCellColor(int index, const Eigen::Vector4<uint8_t>& color)
	{
		for (int i = 0;i < cellArray.size();i++) {
			if (i == index) {
				cellArray[index].color = color;
			}
			else
			{
				cellArray[i].color = { 255,255,255,255 };
			}
		}
		isDirty = true;
	}
	int PolygonMesh::getBlockId(const std::string& name)
	{
		auto it = blockNameToIndex.find(name);
		if (it != blockNameToIndex.end()) {
			return it->second;
		}
		return -1;
	}
	void PolygonMesh::setBlockColor(int index, const Maths::FVector4& color)
	{
		blockColor[index] = color;
		blockColorDirty = true;
	}
	void PolygonMesh::addCell(const PolygonFace& cell)
	{
		mergeBox(cell);
		cellArray.push_back(cell);
		cellArray.back().blockId = nextBlockId;
	}
	void PolygonMesh::switchNextBlock(const Maths::FVector4& color,const std::string& name)
	{
		blockColor.push_back(color);
		blockNameToIndex[name] = nextBlockId;
		nextBlockId++;
	}
	void PolygonMesh::submit()
	{
		std::vector<VertexFormat> vData;
		edgeValue.clear();
		for (int i = 0;i < cellArray.size();i++) {
			PolygonFace& cell = cellArray[i];
			for (int j = 2;j < cell.vertex.size();j++) {
				VertexFormat vd1(cell.vertex[0],cell.blockId, cell.color, cell.n, cell.uv[0]);
				VertexFormat vd2(cell.vertex[j - 1] , cell.blockId, cell.color, cell.n, cell.uv[j - 1]);
				VertexFormat vd3(cell.vertex[j] , cell.blockId, cell.color, cell.n, cell.uv[j]);
				vData.push_back(vd1);
				vData.push_back(vd2);
				vData.push_back(vd3);
				if (drawEdge) {
					if (j == 2) {
						edgeValue.push_back(cell.vertex.size()==3?7:6);
					}
					else if (j == cell.vertex.size() - 1) {
						edgeValue.push_back( 3);
					}
					else
					{
						edgeValue.push_back( 2);
					}
				}
				else
				{
					edgeValue.push_back(0);
				}
			}
		}
		numVertex = vData.size();

		// Recorded, not executed: the vertex array, its layout (the stride is this struct's) and the
		// stream belong to the side that owns the context (Filament's updateBufferObject).
		if (::Rendering::Context::Driver* const driver = ::Rendering::HAL::CurrentDriver())
		{
			::Rendering::Backend::UploadOverlayVerticesRequest request;
			request.vertexArrayHandleId = vaoHandle;
			request.vertexBufferHandleId = vboHandle;
			request.stride = static_cast<uint32_t>(sizeof(VertexFormat));
			request.vertices.assign(reinterpret_cast<const uint8_t*>(vData.data()),
				reinterpret_cast<const uint8_t*>(vData.data()) + vData.size() * sizeof(VertexFormat));
			driver->EnqueueUploadOverlayVertices(std::move(request));
		}
		
		//
		Rendering::Settings::TextureDesc desc;
		desc.isTextureBuffer = true;
		desc.internalFormat = Rendering::Settings::EInternalFormat::R8UI;
		desc.buffetLen = edgeValue.size() * sizeof(uint8_t);
		desc.mutableDesc= Rendering::Settings::MutableTextureDesc{
				.data=edgeValue.data()
		};
		if (std::getenv("MOON_FRAME_TRACE") != nullptr)
		{
			// What the upload carries: identical numbers in two runs mean the CPU side is stable
			// and the difference (if any) is in the object the draw samples.
			uint64_t sum = 0;
			for (const uint8_t v : edgeValue) { sum += v; }
			std::fprintf(stderr, "[im3d] submit faces=%d verts=%d edgeCount=%d edgeSum=%llu\n",
				static_cast<int>(cellArray.size()), static_cast<int>(vData.size()),
				static_cast<int>(edgeValue.size()), static_cast<unsigned long long>(sum));
			std::fflush(stderr);
		}
		EnsureTextureBuffer(edgeTexture, "tbo", desc);
	}
	void PolygonMesh::bind()
	{
		if (isDirty) {
			isDirty = false;
			submit();
		}
		if (blockColorDirty) {
			blockColorDirty = false;
			::Rendering::Settings::TextureDesc desc;
			desc.isTextureBuffer = true;
			desc.internalFormat = ::Rendering::Settings::EInternalFormat::RGBA32F;
			desc.buffetLen = nextBlockId * sizeof(Maths::FVector4);
			desc.mutableDesc = ::Rendering::Settings::MutableTextureDesc{
				.data = blockColor.data()
			};
			if (std::getenv("MOON_FRAME_TRACE") != nullptr)
			{
				uint64_t sum = 0;
				for (const Maths::FVector4& color : blockColor)
				{
					sum += static_cast<uint64_t>(color.x * 255.0f) + static_cast<uint64_t>(color.y * 255.0f)
						+ static_cast<uint64_t>(color.z * 255.0f) + static_cast<uint64_t>(color.w * 255.0f);
				}
				std::fprintf(stderr, "[im3d] blockColors count=%d nextBlockId=%d sum=%llu first=%.3f,%.3f,%.3f\n",
					static_cast<int>(blockColor.size()), nextBlockId,
					static_cast<unsigned long long>(sum),
					blockColor.empty() ? 0.0f : blockColor[0].x,
					blockColor.empty() ? 0.0f : blockColor[0].y,
					blockColor.empty() ? 0.0f : blockColor[0].z);
				std::fflush(stderr);
			}
			EnsureTextureBuffer(blockTexture, "blockColors", desc);
		}
		// The bind is the draw's own: DrawVertexArray binds the array it draws from.
	}
	Eigen::Vector3f PolygonMesh::getCellNormal(int index)
	{
		return cellArray[index].n;
	}
	PolygonMesh::~PolygonMesh()
	{
		using namespace ::Rendering::Backend;
		if (vaoHandle != 0)
		{
			::Rendering::HAL::ResourceOps::DestroyVertexArray(ResourceHandle<HwVertexArray>{ vaoHandle });
			vaoHandle = 0;
		}
		if (vboHandle != 0)
		{
			::Rendering::HAL::ResourceOps::DestroyBuffer(ResourceHandle<HwBufferObject>{ vboHandle });
			vboHandle = 0;
		}
	}
	void PolygonMesh::addMesh(Rendering::Resources::Mesh* mesh,const Maths::FMatrix4& matrix, const Eigen::Vector4<uint8_t>& c)
	{
		int vcnt=mesh->GetVertexCount();
		int icnt = mesh->GetIndexCount();
		int num = icnt > 0 ? icnt : vcnt;
		for (int i = 0; i < num;i+=3) {
			Maths::FVector3 v0 = Maths::FMatrix4::MulPoint(matrix,mesh->GetVertexPosition(i));
			Maths::FVector3 v1 = Maths::FMatrix4::MulPoint(matrix, mesh->GetVertexPosition(i+1));
			Maths::FVector3 v2 = Maths::FMatrix4::MulPoint(matrix, mesh->GetVertexPosition(i+2));
			PolygonFace face({ v0.x,v0.y,v0.z }, { v1.x,v1.y,v1.z }, { v2.x,v2.y,v2.z }, c);
			mergeBox(face);
			cellArray.push_back(face);
			cellArray.back().blockId = nextBlockId;
		}
	}
	void PolygonMesh::addModel(Rendering::Resources::Model* model, const Maths::FMatrix4& matrix, const Eigen::Vector4<uint8_t>& c)
	{
		for (auto m:model->GetMeshes()) {
			addMesh(m,matrix,c);
		}
	}
	void PolygonMesh::initGpuBuffer()
	{
		// Filament's createVertexArray/createBuffer: the slot is taken here, the GL name is made on
		// the side that owns the context. submit() describes the layout and uploads the stream.
		using namespace ::Rendering::Backend;
		vaoHandle = GetHandleAllocator().Allocate<HwVertexArray>().GetId();
		::Rendering::HAL::ResourceOps::EnsureVertexArray(ResourceHandle<HwVertexArray>{ vaoHandle });
		vboHandle = GetHandleAllocator().Allocate<HwBufferObject>().GetId();
		::Rendering::HAL::ResourceOps::EnsureBuffer(ResourceHandle<HwBufferObject>{ vboHandle },
			::Rendering::Settings::EBufferType::VERTEX);
		submit();
	}
	int PolygonMesh::hit(const Eigen::Matrix4f& viewProj, float u, float v)
	{
		int res = -1;
		float minDist = 100000.0f;
		Ray ray;
		ray.m_origin = { u,v,-2 };
		ray.m_direction = { 0,0,1.0 };
		for (int i = 0; i < cellArray.size(); i++) {
			PolygonFace& cell = cellArray[i];
			std::vector<Eigen::Vector3f>ndcPos(cell.vertex.size());
			for (int j = 0;j < cell.vertex.size();j++) {
				ndcPos[j] = MatrixMulPoint(viewProj, cell.vertex[j]);
			}
			for (int j = 2; j < cell.vertex.size(); j++) {
				float tr;
				if (Intersect(ray, ndcPos[0], ndcPos[j - 1], ndcPos[j], tr)) {
					if (tr < minDist) {
						minDist = tr;
						res = i;
					}
				}
			}
		}
		return res;
	}
	void PolygonMesh::setId(int id)
	{
		this->id = id;
	}
	int PolygonMesh::getId()
	{
		return this->id;
	}
	void PolygonMesh::mergeBox(const Eigen::Vector3f& vertex)
	{
		minConner.x() = std::min(minConner.x(), vertex.x());
		minConner.y() = std::min(minConner.y(), vertex.y());
		minConner.z() = std::min(minConner.z(), vertex.z());
		maxConner.x() = std::max(maxConner.x(), vertex.x());
		maxConner.y() = std::max(maxConner.y(), vertex.y());
		maxConner.z() = std::max(maxConner.z(), vertex.z());
	}
	void PolygonMesh::mergeBox(const PolygonFace& face)
	{
		for (int i = 0;i < face.vertex.size();i++) {
			mergeBox(face.vertex[i]);
		}
	}
	ViewCubeLayout ComputeViewCubeLayout(float p_viewportWidth, float p_viewportHeight)
	{
		// Anchor through ScreenLayout so the cube and the screen widgets that
		// decorate it share one implementation of the corner arithmetic.
		ScreenLayout cubeLayout;
		cubeLayout.anchor = EScreenAnchor::TopRight;
		cubeLayout.offset = ScreenPoint(
			static_cast<float>(kViewCubeMargin),
			static_cast<float>(kViewCubeMargin));
		cubeLayout.size = ScreenPoint(
			static_cast<float>(kViewCubeSize),
			static_cast<float>(kViewCubeSize));

		const ScreenRect rect = cubeLayout.Resolve(
			static_cast<int>(p_viewportWidth),
			static_cast<int>(p_viewportHeight));

		ViewCubeLayout layout;
		layout.centerX = rect.x + rect.w * 0.5f;
		layout.centerY = rect.y + rect.h * 0.5f;
		layout.halfSize = rect.w * 0.5f;
		layout.glViewportX = static_cast<int>(rect.x);
		// GL uses a bottom-left origin, the layout a top-left one.
		layout.glViewportY = static_cast<int>(p_viewportHeight - rect.y - rect.h);
		return layout;
	}

	GuiWidgetPolyMesh& NavigateCube()
	{
		static GuiWidgetPolyMesh viewCube;
		if (viewCube.cellArray.size() == 0) {
			viewCube.screenPos.viewportSizeX = kViewCubeSize;
			viewCube.screenPos.viewportSizeY = kViewCubeSize;
			viewCube.screenPos.startX = 20;
			viewCube.screenPos.startY = 20;
			float halflen = 3.0f;
			float shift = 0.6f;
			float ratio = 0.5 * shift / halflen;
			Eigen::Vector3f n = { 0, 0, 1 };
			Eigen::Vector3f A = { -halflen + shift,halflen - shift, halflen };
			Eigen::Vector3f A1 = { -halflen + 2 * shift, halflen - shift, halflen };
			Eigen::Vector3f A2 = { -halflen + shift, halflen - 2 * shift, halflen };
			Eigen::Vector3f B = { halflen - shift, halflen - shift, halflen };
			Eigen::Vector3f B1 = { halflen - 2 * shift, halflen - shift, halflen };
			Eigen::Vector3f B2 = { halflen - shift, halflen - 2 * shift, halflen };
			Eigen::Vector3f C = { halflen - shift, -halflen + shift, halflen };
			Eigen::Vector3f C1 = { halflen - 2 * shift, -halflen + shift, halflen };
			Eigen::Vector3f C2 = { halflen - shift, -halflen + 2 * shift, halflen };
			Eigen::Vector3f D = { -halflen + shift, -halflen + shift, halflen };
			Eigen::Vector3f D1 = { -halflen + 2 * shift, -halflen + shift, halflen };
			Eigen::Vector3f D2 = { -halflen + shift, -halflen + 2 * shift, halflen };
			Eigen::Vector3f F1 = B1;
			Eigen::Vector3f F2 = F1 + Eigen::Vector3f(0, shift, -shift);
			Eigen::Vector3f F3 = F2 + Eigen::Vector3f(shift, 0, -shift);
			Eigen::Vector3f F4 = F3 + Eigen::Vector3f(shift, -shift, 0);
			Eigen::Vector3f F5 = F4 + Eigen::Vector3f(0, -shift, +shift);
			Eigen::Vector3f F6 = B2;
			Eigen::Vector3f F7 = { halflen - 2 * shift, halflen , halflen - shift };
			Eigen::Vector3f F8 = { -halflen + 2 * shift, halflen , halflen - shift };
			Eigen::Vector3f F9 = { halflen , -halflen + 2 * shift, halflen - shift };
			Eigen::Vector3f F10 = { halflen, halflen - 2 * shift, halflen - shift };
			Eigen::Vector3f F11 = { -halflen + 2 * shift, -halflen , halflen - shift };
			Eigen::Vector3f F12 = { halflen - 2 * shift, -halflen , halflen - shift };

			// One face per atlas cell. The UVs are read from the same table the labels are
			// painted with (ViewCubeLabels::FaceUvRect), so the picture and what the geometry
			// samples cannot drift apart - no hand measured offsets left here.
			auto makeFace = [&](int p_face) {
				float u0 = 0.0f, v0 = 0.0f, uw = 1.0f, vh = 1.0f;
				::Rendering::Overlay::ViewCubeLabels::FaceUvRect(p_face, u0, v0, uw, vh);
				const auto uv = [&](float p_rx, float p_ry) {
					return Eigen::Vector2f(u0 + p_rx * uw, v0 + p_ry * vh);
				};
				PolygonFace face;
				face.n = n;
				face.addPoint(A1, uv(0.125f, 1.0f));
				face.addPoint(A2, uv(0.000f, 0.875f));
				face.addPoint(D2, uv(0.000f, 0.125f));
				face.addPoint(D1, uv(0.125f, 0.0f));
				face.addPoint(C1, uv(0.875f, 0.0f));
				face.addPoint(C2, uv(1.000f, 0.125f));
				face.addPoint(B2, uv(1.000f, 0.875f));
				face.addPoint(B1, uv(0.875f, 1.0f));
				return face;
			};

			viewCube.addCell(makeFace(0));
			viewCube.addCell(makeFace(1).transform(EulerXYZToMatrix4Degree({ 90, 0, 0 })));
			viewCube.addCell(makeFace(2).transform(EulerXYZToMatrix4Degree({ 180, 0, 0 })));
			viewCube.addCell(makeFace(3).transform(EulerXYZToMatrix4Degree({ 270, 0, 0 })));
			viewCube.addCell(makeFace(4).transform(EulerXYZToMatrix4Degree({ 0, 90, 0 })));
			viewCube.addCell(makeFace(5).transform(EulerXYZToMatrix4Degree({ 0, 270, 0 })));
			PolygonFace cell;   // the chamfer quads below reuse it
			cell.clear();
			cell.n = Eigen::Vector3f(1, 1, 1).normalized();
			cell.addPointArray({ F1,F2,F3,F4,F5,F6 }, { { 0, 0 }, { 0,0 }, { 0,0 }, { 0,0 }, { 0,0 }, { 0,0 } });
			viewCube.addCell(cell);
			viewCube.addCell(cell.transform(EulerXYZToMatrix4Degree({ 0, 90, 0 })));
			viewCube.addCell(cell.transform(EulerXYZToMatrix4Degree({ 0, 180, 0 })));
			viewCube.addCell(cell.transform(EulerXYZToMatrix4Degree({ 0, 270, 0 })));
			viewCube.addCell(cell.transform(EulerXYZToMatrix4Degree({ 90, 0, 0 })));
			viewCube.addCell(cell.transform(EulerXYZToMatrix4Degree({ 180, 0, 0 })));
			viewCube.addCell(cell.transform(EulerXYZToMatrix4Degree({ 90, -90, 0 })));
			viewCube.addCell(cell.transform(EulerXYZToMatrix4Degree({ 90, -180, 0 })));
			cell.clear();
			cell.n = Eigen::Vector3f(0, 1, 1).normalized();
			cell.addPointArray({ A1,B1,F7,F8 }, { {0,0},{0,0 },{0,0} ,{0,0} });
			viewCube.addCell(cell);
			viewCube.addCell(cell.transform(EulerXYZToMatrix4Degree({ 0, 90, 0 })));
			viewCube.addCell(cell.transform(EulerXYZToMatrix4Degree({ 0, 180, 0 })));
			viewCube.addCell(cell.transform(EulerXYZToMatrix4Degree({ 0, 270, 0 })));
			cell.clear();
			cell.n = Eigen::Vector3f(1, 0, 1).normalized();
			cell.addPointArray({ C2,F9,F10,B2 }, { { 0, 0 }, { 0,0 }, { 0,0 }, { 0,0 } });
			viewCube.addCell(cell);
			viewCube.addCell(cell.transform(EulerXYZToMatrix4Degree({ 0, 90, 0 })));
			viewCube.addCell(cell.transform(EulerXYZToMatrix4Degree({ 0, 180, 0 })));
			viewCube.addCell(cell.transform(EulerXYZToMatrix4Degree({ 0, 270, 0 })));
			cell.clear();
			cell.n = Eigen::Vector3f(0, -1, 1).normalized();
			cell.addPointArray({ D1,F11,F12,C1 }, { { 0, 0 }, { 0,0 }, { 0,0 }, { 0,0 } });
			viewCube.addCell(cell);
			viewCube.addCell(cell.transform(EulerXYZToMatrix4Degree({ 0, 90, 0 })));
			viewCube.addCell(cell.transform(EulerXYZToMatrix4Degree({ 0, 180, 0 })));
			viewCube.addCell(cell.transform(EulerXYZToMatrix4Degree({ 0, 270, 0 })));		
			viewCube.initGpuBuffer();
			std::string texturePath = Tools::Utils::PathParser::GetExeDirectory() + "/Moon/Data/Engine/Textures/XYZ.png";
			viewCube.texture = Core::Global::ServiceLocator::Get<Core::ResourceManagement::TextureManager>().GetResource(texturePath, true);	
		}
		return viewCube;
	}
	void UpdateViewCubeLabels()
	{
		using namespace Rendering::Resources;
		namespace Settings = Rendering::Settings;

		GuiWidgetPolyMesh& cube = NavigateCube();
		// The atlas is generated at 3x2 cells; 256 px per cell is far more than the cube covers on
		// screen, which is what keeps the labels crisp instead of resampled from a fixed PNG.
		// One cell per face at roughly twice the on-screen size: bigger than that and the mip chain
		// blurs the labels instead of sharpening them.
				std::vector<uint8_t> atlasPixels;
		uint32_t atlasWidth = 0;
		uint32_t atlasHeight = 0;
		// The pixels come from the engine's hook: the editor paints the labels, the engine owns the
		// texture (see Rendering::Overlay::ViewCubeLabels::BuildAtlas).
		Rendering::Overlay::ViewCubeLabels::BuildAtlas(192, atlasPixels, atlasWidth, atlasHeight);
		if (atlasPixels.empty()) { static bool reported = false; if (!reported) { reported = true; std::fprintf(stderr, "[viewcube] label atlas empty: the editor painter is not registered\n"); std::fflush(stderr); } return; }

		static Rendering::Resources::Texture* s_labelTexture = nullptr;
		if (s_labelTexture == nullptr)
		{
			s_labelTexture = Loaders::TextureLoader::CreateMutable(
				atlasWidth,
				atlasHeight,
				atlasPixels.data(),
				Settings::ETextureFilteringMode::LINEAR,
				Settings::ETextureFilteringMode::LINEAR,
				Settings::ETextureWrapMode::CLAMP_TO_EDGE,
				Settings::ETextureWrapMode::CLAMP_TO_EDGE,
				false);
		}
		else
		{
			/* The HAL object lives on the executor, so uploading into it here would do nothing. Rebuild the atlas through the command stream instead: CreateMutable records a create command carrying the new pixels, and the old texture is released by its own destroy command. */ delete s_labelTexture; s_labelTexture = Loaders::TextureLoader::CreateMutable(atlasWidth, atlasHeight, atlasPixels.data(), Settings::ETextureFilteringMode::LINEAR, Settings::ETextureFilteringMode::LINEAR, Settings::ETextureWrapMode::CLAMP_TO_EDGE, Settings::ETextureWrapMode::CLAMP_TO_EDGE, false);

		}

		if (s_labelTexture != nullptr)
		{
			cube.texture = s_labelTexture;
		}
		::Rendering::Overlay::ViewCubeLabels::MarkClean();
	}

	PolygonMesh& CoordAxis()
	{
		static PolygonMesh viewAxis;
		if (viewAxis.cellArray.size() == 0) {
			float halflen = 3.0f;
			Maths::FMatrix4 model =
				Maths::FMatrix4::Translation({ 0,0,0 }) *
				Maths::FMatrix4::Scaling({ 6,6,6 });
			viewAxis.drawEdge = false;
			
			auto arrow = GetModelService[":Models/Arrow_Translate.fbx"];
			auto sphere= GetModelService[":Models/Sphere.fbx"];
			viewAxis.addModel(arrow, model, { 0,0,255,255 });
			viewAxis.switchNextBlock({0,0,1,1});
			viewAxis.addModel(arrow, model.RotateOnAxisY(-90), { 255,0,0,255 });
			viewAxis.switchNextBlock({ 1,0,0,1 });
			viewAxis.addModel(arrow, model.RotateOnAxisX(-90), { 0,255,0,255 });
			viewAxis.switchNextBlock({ 0,1,0,1 });
			viewAxis.addModel(sphere, Maths::FMatrix4::Translation({ 0,0,0 }) * Maths::FMatrix4::Scaling({ 0.5f,0.5f,0.5f }), { 255,255,0,255 });
			viewAxis.switchNextBlock({ 1,1,0,1 });
			viewAxis.model = Eigen::Matrix4f::Identity();
			viewAxis.model(0, 3) = -halflen;
			viewAxis.model(1, 3) = -halflen;
			viewAxis.model(2, 3) = -halflen;
			viewAxis.initGpuBuffer();
			std::string texturePath = Tools::Utils::PathParser::GetExeDirectory() + "/Moon/Data/Engine/Textures/XYZ.png";
			viewAxis.texture = Core::Global::ServiceLocator::Get<Core::ResourceManagement::TextureManager>().GetResource(texturePath, true);
		}
		return viewAxis;
	}
	PolygonMesh& TransformAxis()
	{
		static PolygonMesh poly;
		if (poly.cellArray.size() == 0) {
			float halflen = 3.0f;
			Maths::FMatrix4 model =
				Maths::FMatrix4::Translation({ 0,0,0 }) *
				Maths::FMatrix4::Scaling({ 6,6,6 });
			poly.drawEdge = false;

			auto arrow = GetModelService[":Models/Arrow_Translate.fbx"];
			auto sphere = GetModelService[":Models/Sphere.fbx"];
			auto cil= GetModelService[":Models/res.obj"];
			poly.addModel(cil, Maths::FMatrix4::Identity, { 255,255,255,255 });
			poly.switchNextBlock({ 1,0,0,1 },"XAxis");
			poly.addModel(cil, Maths::FMatrix4::Identity.RotateOnAxisZ(90.0f).RotateOnAxisX(90), {255,255,255,255});
			poly.switchNextBlock({ 0,0,1,1 },"YAxis");
			poly.addModel(cil, Maths::FMatrix4::Identity.RotateOnAxisZ(90.0f).RotateOnAxisY(90), {255,255,255,255});
			poly.switchNextBlock({ 1,0,1,1 } , "ZAxis");
			poly.addModel(arrow, model, { 255,255,255,255 });
			poly.switchNextBlock({ 0,0,1,1 },"ZArrow");
			poly.addModel(arrow, model.RotateOnAxisY(-90), { 255,255,255,255 });
			poly.switchNextBlock({ 1,0,0,1 },"XArrow");
			poly.addModel(arrow, model.RotateOnAxisX(-90), { 255,255,255,255 });
			poly.switchNextBlock({ 0,1,0,1 },"YArrow");
			poly.addModel(sphere, Maths::FMatrix4::Translation({ 0,0,0 }) * Maths::FMatrix4::Scaling({ 0.5f,0.5f,0.5f }), { 255,255,255,255 });
			poly.switchNextBlock({ 1,1,0,1 });
			float quadlen = 3.0;
			float quadoffset = 1.0;
			//y axis
			PolygonFace cell;
			cell.addPoint({ quadoffset,0,quadoffset }, { 0,0 });
			cell.addPoint({ quadoffset+quadlen,0,quadoffset  }, { 0,0 });
			cell.addPoint({ quadoffset + quadlen,0,quadoffset +quadlen }, { 0,0 });
			cell.addPoint({ quadoffset,0,quadoffset +quadlen }, { 0,0 });
			cell.n = {0,1,0};
			poly.addCell(cell);
			poly.switchNextBlock({0,1,0,1},"YPlane");
			
			
			//x axis
			cell.clear();
			cell.addPoint({ 0,quadoffset,quadoffset }, { 0,0 });
			cell.addPoint({ 0,quadoffset + quadlen,quadoffset  }, { 0,0 });
			cell.addPoint({ 0,quadoffset + quadlen,quadoffset + quadlen }, { 0,0 });
			cell.addPoint({ 0,quadoffset ,quadoffset + quadlen }, { 0,0 });
			cell.n = { 1,0,0 };
			poly.addCell(cell);
			poly.switchNextBlock({ 1,0,0,1 },"XPlane");
			//z axis
			cell.clear();
			cell.addPoint({ quadoffset,quadoffset,0 }, { 0,0 });
			cell.addPoint({ quadoffset + quadlen,quadoffset,0}, { 0,0 });
			cell.addPoint({ quadoffset + quadlen,quadoffset + quadlen,0 }, { 0,0 });
			cell.addPoint({ quadoffset ,quadoffset + quadlen,0}, { 0,0 });
			cell.n = { 0,0,1 };
			poly.addCell(cell);
			poly.switchNextBlock({ 0,0,1,1 },"ZPlane");


			poly.model = Eigen::Matrix4f::Identity();
		
			poly.initGpuBuffer();
			std::string texturePath = Tools::Utils::PathParser::GetExeDirectory() + "/Moon/Data/Engine/Textures/XYZ.png";
			poly.texture = Core::Global::ServiceLocator::Get<Core::ResourceManagement::TextureManager>().GetResource(texturePath, true);
		}
		return poly;
	}
	PolygonMesh& GizmoSketchPlane()
	{
		static PolygonMesh poly;
		if (poly.cellArray.size() == 0) {
			float halflen = 3.0f;
			Maths::FMatrix4 model =Maths::FMatrix4::Translation({ 0,0,0 }) * Maths::FMatrix4::Scaling({ 6,6,6 });
			poly.drawEdge = false;
			auto arrow = GetModelService[":Models/Arrow_Translate.fbx"];
			auto sphere = GetModelService[":Models/Sphere.fbx"];
			poly.addModel(arrow, model, { 255,255,255,255 });
			poly.switchNextBlock({ 0,0,1,1 }, "ZArrow");
			poly.addModel(arrow, model.RotateOnAxisY(-90), { 255,255,255,255 });
			poly.switchNextBlock({ 1,0,0,1 }, "XArrow");
			poly.addModel(arrow, model.RotateOnAxisX(-90), { 255,255,255,255 });
			poly.switchNextBlock({ 0,1,0,1 }, "YArrow");
			poly.addModel(sphere, Maths::FMatrix4::Translation({ 0,0,0 }) * Maths::FMatrix4::Scaling({ 0.5f,0.5f,0.5f }), { 255,255,0,255 });
			poly.switchNextBlock({ 1,1,0,1 });
			float quadlen = 3.0;
			float quadoffset = 1.0;
			//y axis
			PolygonFace cell;
			cell.addPoint({ quadoffset,0,quadoffset }, { 0,0 });
			cell.addPoint({ quadoffset + quadlen,0,quadoffset }, { 1.0 / 3,0 });
			cell.addPoint({ quadoffset + quadlen,0,quadoffset + quadlen }, { 1.0/3,1 });
			cell.addPoint({ quadoffset,0,quadoffset + quadlen }, { 0,1 });
			cell.n = { 0,1,0 };
			poly.addCell(cell);
			poly.switchNextBlock({ 1,1,1,0.7 }, "YPlane");
			//x axis
			cell.clear();
			cell.addPoint({ 0,quadoffset,quadoffset }, { 1.0/3,0 });
			cell.addPoint({ 0,quadoffset + quadlen,quadoffset }, { 2.0/3,0 });
			cell.addPoint({ 0,quadoffset + quadlen,quadoffset + quadlen }, { 2.0/3,1 });
			cell.addPoint({ 0,quadoffset ,quadoffset + quadlen }, { 1.0/3,1 });
			cell.n = { 1,0,0 };
			poly.addCell(cell);
			poly.switchNextBlock({ 1,1,1,0.7 }, "XPlane");
			//z axis
			cell.clear();
			cell.addPoint({ quadoffset,quadoffset,0 }, { 2.0/3,0 });
			cell.addPoint({ quadoffset + quadlen,quadoffset,0 }, { 1,0});
			cell.addPoint({ quadoffset + quadlen,quadoffset + quadlen,0 }, { 1,1 });
			cell.addPoint({ quadoffset ,quadoffset + quadlen,0 }, { 2.0/3,1 });
			cell.n = { 0,0,1 };
			poly.addCell(cell);
			poly.switchNextBlock({ 1,1,1,0.7 }, "ZPlane");

			poly.model = Eigen::Matrix4f::Identity();
			poly.initGpuBuffer();
			std::string texturePath = Tools::Utils::PathParser::GetExeDirectory() + "/Moon/Data/Engine/Textures/plane.png";
			poly.texture = Core::Global::ServiceLocator::Get<Core::ResourceManagement::TextureManager>().GetResource(texturePath, true);
		}
		return poly;
	}
}
