#include <glad/glad.h>
#include <cstdlib>
#include "editor/ViewCubeLabels.h"
#include "Qtimgui/imguiwidgets/QtImGui.h"
#include <ThirdParty/imgui/imgui/imgui.h>
#include <ThirdParty/imgui/imgui/imgui_internal.h>
#include <ThirdParty/imgui/implot/implotCustom.h>
#include <ThirdParty/imgui/implot/imGuizmo.h>
#include "Im3DRenderer.h"
#include <Rendering/Overlay/Im2DRender.h>
#include <Rendering/Overlay/Im2D.h>
#include <Rendering/Overlay/Im2DFont.h>
#include "Interactive/MathUtil/MathUtil.h"
#include "Interactive/Im3DPalette.h"
#include "Interactive/Effect/LineDashEffect.h"
#include "Interactive/Effect/PlaneGridEffect.h"
#include "Settings/DebugSetting.h"
#include "core/Global/ServiceLocator.h"
#include "renderer/SceneView.h"
#include "renderer/Context.h"
#include "Core/Global/ServiceLocator.h"
#include "EventWidget.h"
#include "Interactive/Screen/ScreenOverlayRegistry.h"
#include "Sketcher/SketcherObj.h"
#include <algorithm>
#include <cmath>
#include <cstring>

#include "editor/View/sceneview/viewerwidget.h"
#include <Rendering/Data/Material.h>
#include <Rendering/Resources/Loaders/ShaderLoader.h>
#include <Maths/FMatrix4.h>
#include <iostream>
#include <Tools/Utils/PathParser.h>
namespace MOON
{

	static QtImGui::RenderRef imref = nullptr;
	static ImPlotContext* ctx = nullptr;
	static bool isBeginImgui = false;

	static Maths::FMatrix4 ToFMatrix4(const Eigen::Matrix4f& mat) {
		Eigen::Matrix4f temp = mat;
		temp.transposeInPlace();
		Maths::FMatrix4 ret;
		memcpy(ret.data, temp.data(), 16 * sizeof(float));
		return ret;
	}
	static Eigen::Matrix4f ToEigenMatrix4f(const Maths::FMatrix4& mat) {
		Eigen::Matrix4f ret(mat.data);
		ret.transposeInPlace();
		return ret;
	}
	static constexpr unsigned int kFnv1aPrime32 = 0x01000193u;
	constexpr auto kWhite = std::to_array<uint8_t>({ 255, 255, 255, 255 });
	constexpr auto kBlack = std::to_array<uint8_t, 6 * 4>({ 0 });
	ImRenderer::ImRenderer()
		:  
		mEmptyTexture2D{ Rendering::Settings::ETextureType::TEXTURE_2D },
		mEmptyTextureCube{ Rendering::Settings::ETextureType::TEXTURE_CUBE }
	{
		const auto kEmptyTextureDesc = Rendering::Settings::TextureDesc{
	.width = 1,
	.height = 1,
	.minFilter = Rendering::Settings::ETextureFilteringMode::NEAREST,
	.magFilter = Rendering::Settings::ETextureFilteringMode::NEAREST,
	.horizontalWrap = Rendering::Settings::ETextureWrapMode::REPEAT,
	.verticalWrap = Rendering::Settings::ETextureWrapMode::REPEAT,
	.internalFormat = Rendering::Settings::EInternalFormat::RGBA8,
	.useMipMaps = false
		};

		mEmptyTexture2D.Allocate(kEmptyTextureDesc);
		mEmptyTexture2D.Upload(kWhite.data(), Rendering::Settings::EFormat::RGBA, Rendering::Settings::EPixelDataType::UNSIGNED_BYTE);

		mEmptyTextureCube.Allocate(kEmptyTextureDesc);
		mEmptyTextureCube.Upload(kBlack.data(), Rendering::Settings::EFormat::RGBA, Rendering::Settings::EPixelDataType::UNSIGNED_BYTE);
	}

	ImRenderer& ImRenderer::instance()
	{
		static ImRenderer ImRenderer;
		return ImRenderer;
	}

	// ---- custom drawing effects -----------------------------------------------------------
	AImEffect* ImRenderer::registerEffect(std::unique_ptr<AImEffect> p_effect)
	{
		if (p_effect == nullptr)
		{
			return nullptr;
		}
		if (AImEffect* existing = findEffect(p_effect->Id()))
		{
			return existing;
		}
		AImEffect* raw = p_effect.get();
		mEffects.push_back(std::move(p_effect));
		return raw;
	}

	AImEffect* ImRenderer::findEffect(const std::string& p_id) const
	{
		for (const std::unique_ptr<AImEffect>& effect : mEffects)
		{
			if (effect->Id() == p_id)
			{
				return effect.get();
			}
		}
		return nullptr;
	}

	void ImRenderer::drawEffects(EImChannel p_channel)
	{
		// The only thing the renderer knows about an effect: which channel it belongs to and that
		// it has to be drawn once per frame. The effect already has the frame context (BeginFrame)
		// and its own geometry, so this is the whole relationship.
		for (const std::unique_ptr<AImEffect>& effect : mEffects)
		{
			if (effect->Desc().channel == p_channel)
			{
				effect->Draw(m_overlayGl);
			}
		}
	}

	void ImRenderer::initGizmoGl()
	{
		// Vertex array, vertex buffer and the gizmo materials: the gizmo pass draws with them
		// inside the render module's frame, so they belong to the module's context.
		prepareGl();
	}

	void ImRenderer::init()
	{
		// ImGui and ImPlot only: they draw after the frame has been pasted, on the viewport's
		// context, and QtImGui::initialize creates the font texture that drawing uses.
		registerDebugSettings();
		// ResetId();
		sortCalled = false;
		endFrameCalled = false;
		primMode = PrimitiveModeNone;
		vertexDataIndex = 0; // = sorting disabled
		layerIndex = 0;
		firstVertThisPrim = 0;
		vertCountThisPrim = 0;

		// m_gizmoLocal = true;
		// m_gizmoMode = GizmoMode_Rotation;
		hotId = IdInvalid;
		activeId = IdInvalid;
		hotId2D = IdInvalid;
		activeId2D = IdInvalid;
		appId = IdInvalid;
		appActiveId = IdInvalid;
		appHotId = IdInvalid;
		hotDepth = FLT_MAX;

		memset(&keyDownCurr, 0, sizeof(keyDownCurr));
		memset(&keyDownPrev, 0, sizeof(keyDownPrev));
		matrixStack.push_back(Eigen::Matrix4f::Identity());
		colorStack.push_back(Eigen::Vector4<uint8_t>(255, 255, 255, 255));
		alphaStack.push_back(1.0f);
		sizeStack.push_back(1.0f);
		pushEnableSorting(false);
		pushEnableLit(false);
		pushLayerId(0);
		pushId(0x811C9DC5u);

		
		imref = QtImGui::initialize(&GetService(ViewerWidget), false);
		ctx = ImPlot::CreateContext();
	}
	void ImRenderer::preStoreMesh()
	{
		mPreStorePolygon["CoordAxis"] = &CoordAxis();
		mPreStorePolygon["TransformAxis"] = &TransformAxis();
		mPreStorePolygon["NavigateCube"] = &NavigateCube();
		mPreStorePolygon["GizmoSketchPlane"] = &GizmoSketchPlane();
		mPreStorePolygon["CoordAxis"]->setId(0);
		mPreStorePolygon["TransformAxis"]->setId(1);
		mPreStorePolygon["NavigateCube"]->setId(2);
		mPreStorePolygon["GizmoSketchPlane"]->setId(3);
		mPreStorePolygonId[0] = "CoordAxis";
		mPreStorePolygonId[1] = "TransformAxis";
		mPreStorePolygonId[2] = "ViewCube";
		mPreStorePolygonId[3] = "GizmoSketchPlane";
	}
	void ImRenderer::prepareGl()
	{
		//shader leak
		std::string shaderPath =Tools::Utils::PathParser::GetExeDirectory()+"/Moon/Data/Engine/Shaders/";
		
		mTextMaterial = new Rendering::Data::Material(Rendering::Resources::Loaders::ShaderLoader::Create(shaderPath + "/GizmoText.shader"));
		mLineMaterial = new Rendering::Data::Material(Rendering::Resources::Loaders::ShaderLoader::Create(shaderPath + "/GizmoLine.shader"));
		mPointMaterial = new Rendering::Data::Material(Rendering::Resources::Loaders::ShaderLoader::Create(shaderPath + "/GizmoPoint.shader"));
		mTriangleMaterial = new Rendering::Data::Material(Rendering::Resources::Loaders::ShaderLoader::Create(shaderPath + "/GizmoTriangle.shader"));
		mCellMaterial = new Rendering::Data::Material(Core::Global::ServiceLocator::Get<Editor::Core::Context>().shaderManager[":Shaders\\GizmoCell.shader"]);
		mCellMaterial->AddFeature("WITH_EDGE");
		mCellMaterial->AddFeature("CUSTOM_PROJECT");
		mCellMaterial->AddFeature("CUSTOM_VIEWPORT");
		// The lit overlay's own material: a two-sided, vertex-coloured shaded triangle, not the
		// scene's PBR one (that one needs the engine uniforms and model-space input the overlay
		// pass does not have). See GizmoLit.shader.
		mLitMaterial = new Rendering::Data::Material(Rendering::Resources::Loaders::ShaderLoader::Create(shaderPath + "/GizmoLit.shader"));
		mLitMaterial->SetBackfaceCulling(false);
		mLitMaterial->SetCastShadows(false);
		mLitMaterial->SetReceiveShadows(false);

		// The vertex array, buffer and attribute layout are the render module's: it owns the
		// upload and the draw (Rendering::Overlay::Im3DGLRenderer).
		m_overlayGl.Initialize();

		// The custom effects. Each one owns its material and everything else it needs; the
		// renderer only keeps them by id and hands them their runs.
		//
		// Line effects draw in the overlay channel: the sketch's construction curves are drawn
		// without a depth test (the scene never hides what is being edited), and that is the
		// channel this effect serves.
		registerEffect(std::make_unique<LineDashEffect>());
		// The sketch's background grid, in the backdrop channel (behind the axes and the geometry).
		registerEffect(std::make_unique<PlaneGridEffect>());

		preStoreMesh();
	}
	void ImRenderer::registerDebugSettings()
	{

	}
	void ImRenderer::terminate()
	{
		// free resource

		delete mLineMaterial;
		delete mPointMaterial;
		delete mTriangleMaterial;
		delete mCellMaterial;
		delete mLitMaterial;
		// Effect materials own GL programs, so they go down with the other overlay materials.
		mEffects.clear();
	}
    void ImRenderer::selectPolygon(int pid, int bid)
	{
		selectBlockId = bid;
		selectPolygonId = pid;
	}
	bool ImRenderer::isSelectPolygon(const std::string& pname, const std::string& bname)
	{
		return mPreStorePolygon[pname]->getId() == selectPolygonId && mPreStorePolygon[pname]->getBlockId(bname) == selectBlockId;
	}
	void ImRenderer::resetSelectPolygon()
	{
		selectPolygon(-1,-1);
	}
	void ImRenderer::begin(PrimitiveMode _mode)
	{
		assert(!endFrameCalled); // Begin*() called after EndFrame() but before NewFrame(), or forgot to call NewFrame()
		assert(primMode == PrimitiveModeNone); // forgot to call End()
		primMode = _mode;
		vertCountThisPrim = 0;
		switch (primMode)
		{
		case PrimitiveModePoints:
			primType = DrawPrimitivePoints;
			break;
		case PrimitiveModeLines:
		case PrimitiveModeLineStrip:
		case PrimitiveModeLineLoop:
			primType = DrawPrimitiveLines;
			break;
		case PrimitiveModeTriangles:
		case PrimitiveModeTriangleStrip:
			primType = DrawPrimitiveTriangles;
			break;
		default:
			break;
		};
		firstVertThisPrim = static_cast<unsigned int>(getCurrentVertexList()->size());
	}
	void ImRenderer::end()
	{
		assert(primMode != PrimitiveModeNone); // End() called without Begin*()
		if (vertCountThisPrim > 0)
		{
			VertexList* vertexList = getCurrentVertexList();
			switch (primMode)
			{
			case PrimitiveModePoints:
				break;
			case PrimitiveModeLines:
				assert(vertCountThisPrim % 2 == 0);
				break;
			case PrimitiveModeLineStrip:
				assert(vertCountThisPrim > 1);
				break;
			case PrimitiveModeLineLoop:
				assert(vertCountThisPrim > 1);
				vertexList->push_back(vertexList->back());
				vertexList->push_back((*vertexList)[firstVertThisPrim]);
				break;
			case PrimitiveModeTriangles:
				assert(vertCountThisPrim % 3 == 0);
				break;
			case PrimitiveModeTriangleStrip:
				assert(vertCountThisPrim >= 3);
				break;
			default:
				break;
			};
		}
		primMode = PrimitiveModeNone;
		primType = DrawPrimitiveCount;
	}
	void ImRenderer::vertex(const Eigen::Vector3f& p, const Eigen::Vector4f& c, const Eigen::Vector3f& n, const Eigen::Vector2f& tex)
	{
		VertexFormat vd(p, c, n, tex);
		appendLitVertex(vd, p, n);
		litVertexArray.push_back(vd);
	}

	void ImRenderer::vertex(const Eigen::Vector3f& p, const Eigen::Vector4<uint8_t>& c, const Eigen::Vector3f& n, const Eigen::Vector2f& tex)
	{
		VertexFormat vd(p, c, n, tex);
		appendLitVertex(vd, p, n);
		litVertexArray.push_back(vd);
	}
	void ImRenderer::vertex(const Eigen::Vector3f& _position, float _size, const Eigen::Vector4<uint8_t>& _color)
	{
		assert(primMode != PrimitiveModeNone); // Vertex() called without Begin*()

		VertexFormat vd(_position, _size, _color);

		if (matrixStack.size() > 1) // optim, skip the matrix multiplication when the stack size is 1
		{
			Eigen::Vector3f pos = MatrixMulPoint(matrixStack.back(), _position);

			vd.positionSize = Eigen::Vector4<float>(pos.x(), pos.y(), pos.z(), _size);
		}
		vd.color(0) = vd.color(0) * alphaStack.back();

		VertexList* vertexList = getCurrentVertexList();
		switch (primMode)
		{
		case PrimitiveModePoints:
		case PrimitiveModeLines:
		case PrimitiveModeTriangles:
			vertexList->push_back(vd);
			break;
		case PrimitiveModeLineStrip:
		case PrimitiveModeLineLoop:
			if (vertCountThisPrim >= 2)
			{
				vertexList->push_back(vertexList->back());
				++vertCountThisPrim;
			}
			vertexList->push_back(vd);
			break;
		case PrimitiveModeTriangleStrip:
			if (vertCountThisPrim >= 3)
			{
				vertexList->push_back(*(vertexList->end() - 2));
				vertexList->push_back(*(vertexList->end() - 2));
				vertCountThisPrim += 2;
			}
			vertexList->push_back(vd);
			break;
		default:
			break;
		};
		++vertCountThisPrim;
	}



	void ImRenderer::drawOneMesh(const Eigen::Vector3f& translation,const Eigen::Matrix3f& rotation, const Eigen::Vector3f& scale,
		const std::string& mesh, bool longterm)
	{
		drawOneMesh(Coord3(translation, rotation, scale), mesh, longterm);
	}

	void ImRenderer::drawOneMesh(Eigen::Vector3f& translation, Eigen::Matrix3f& rotation, Eigen::Vector3f& scale,
		Eigen::Vector3f& color, const std::string& mesh, bool longterm)
	{
		//drawOneMesh(Coord3(translation, rotation, scale), mesh, color, longterm);
	}

	void ImRenderer::drawOneMesh(
		Eigen::Vector3f translation, Eigen::Vector3f scale, const std::string& mesh, bool longterm)
	{
		//drawOneMesh(Coord3(translation, Eigen::Matrix3f::Identity(), scale), mesh, longterm);
	}

	void ImRenderer::drawOneMesh(
		Eigen::Vector3f translation, Eigen::Vector3f scale, Eigen::Vector3f& color, const std::string& mesh, bool longterm)
	{
		//drawOneMesh(Coord3(translation, Eigen::Matrix3f::Identity(), scale), mesh, color, longterm);
	}

	void ImRenderer::drawOneMesh(const Eigen::Matrix4f& model, const std::string& name, bool longterm)
	{
		if (longterm)
		{
			drawLongTermMeshList.push_back({ name, model });
		}
		else
		{
			drawMeshList.push_back({ name,model });
		}
	}

	void ImRenderer::drawOneMesh(const Eigen::Matrix4f& model, const std::string& mesh, Eigen::Vector3f& color, bool longterm)
	{
		if (longterm)
		{
			drawLongTermMeshList.push_back({ mesh, model,color });
		}
		else
		{
			drawMeshList.push_back({ mesh, model,color });
		}
	}

	void ImRenderer::drawOneFixScaleMesh(Eigen::Matrix4f& model, const std::string& mesh, Eigen::Vector3f& color, bool longterm)
	{
		PolygonInstance me = { mesh, model, color };
		me.fixScaled = true;
		if (longterm)
		{
			drawLongTermMeshList.push_back(me);
		}
		else
		{
			drawMeshList.push_back(me);
		}
	}

	void ImRenderer::drawPoint(const Eigen::Vector3f& _position, float _size, Eigen::Vector4<uint8_t> _color)
	{
		begin(PrimitiveModePoints);
		vertex(_position, _size, _color);
		end();
	}

	void ImRenderer::drawPoint(const Eigen::Vector3f& pos)
	{
		drawPoint(pos, sizeStack.back(), colorStack.back());
	}

	void ImRenderer::drawPoint(const Eigen::Vector3f& pos, float size)
	{
		drawPoint(pos, size, colorStack.back());
	}

	void ImRenderer::drawPointList(const std::vector<Eigen::Vector3f>& position, float size, Eigen::Vector4<uint8_t> color)
	{
		std::vector<VertexFormat> vd(position.size());
		float alpha = alphaStack.back();
		if (matrixStack.size() > 1) // optim, skip the matrix multiplication when the stack size is 1
		{
			for (int i = 0; i < position.size(); i++)
			{
				vd[i] = { MatrixMulPoint(matrixStack.back(), position[i]),size,color };
				vd[i].color[0] *= alpha;
			}
		}
		else
		{
			for (int i = 0; i < position.size(); i++)
			{
				vd[i] = { position[i], size, color };
				vd[i].color[0] *= alpha;
			}
		}
		VertexList* vertexList = vertexData[vertexDataIndex][layerIndex * DrawPrimitiveCount + DrawPrimitivePoints];
		vertexList->insert(vertexList->end(), vd.begin(), vd.end());
	}

	void ImRenderer::drawText3D(const Eigen::Vector3f& p_position, const char* p_text,
		float p_pixelSize, Eigen::Vector4<uint8_t> p_color,
		const Eigen::Vector3f& p_right, const Eigen::Vector3f& p_up, float p_worldPerPixel)
	{
		if (p_text == nullptr || *p_text == '\0' || p_pixelSize <= 0.0f)
		{
			return;
		}
		auto& font = Rendering::Overlay::Im2DRender::Instance().GetFont();
		if (!font.IsLoaded())
		{
			return;
		}

		const Eigen::Vector3f right = p_right.norm() > 0.0f ? p_right.normalized() : Eigen::Vector3f(1, 0, 0);
		const Eigen::Vector3f up = p_up.norm() > 0.0f ? p_up.normalized() : Eigen::Vector3f(0, 1, 0);
		int runAtlas = -1;
		float penX = 0.0f;
		float penY = 0.0f;
		const float lineHeight = font.GetLineHeight(p_pixelSize);

		// UTF-8, one glyph at a time; the font bakes the atlas for this size on first use.
		const unsigned char* it = reinterpret_cast<const unsigned char*>(p_text);
		while (*it != 0)
		{
			uint32_t codepoint = 0;
			if (*it < 0x80) { codepoint = *it++; }
			else if ((*it & 0xE0) == 0xC0) { codepoint = ((*it & 0x1F) << 6) | (it[1] & 0x3F); it += 2; }
			else if ((*it & 0xF0) == 0xE0) { codepoint = ((*it & 0x0F) << 12) | ((it[1] & 0x3F) << 6) | (it[2] & 0x3F); it += 3; }
			else { codepoint = ((*it & 0x07) << 18) | ((it[1] & 0x3F) << 12) | ((it[2] & 0x3F) << 6) | (it[3] & 0x3F); it += 4; }

			if (codepoint == '\n')
			{
				penX = 0.0f;
				penY += lineHeight;
				continue;
			}
			const Rendering::Overlay::Im2D::Glyph* glyph = font.FindGlyph(codepoint, p_pixelSize);
			if (glyph == nullptr)
			{
				continue;
			}
			if (glyph->atlasIndex != runAtlas)
			{
				textRuns.push_back({ static_cast<int>(textVertexArray.size()), 0, glyph->atlasIndex, pendingTextOverlay });
				runAtlas = glyph->atlasIndex;
			}

			// offsetY is measured from the baseline down, up is up: the quad's top edge is the
			// baseline lifted by offsetY, its bottom edge one glyph height below that.
			const float scale = p_worldPerPixel > 0.0f ? p_worldPerPixel : 1.0f;
			const Eigen::Vector3f origin = p_position
				+ right * ((penX + glyph->offsetX) * scale)
				- up * ((glyph->offsetY + penY) * scale);
			const Eigen::Vector3f across = right * (glyph->width * scale);
			const Eigen::Vector3f down = up * (-glyph->height * scale);
			const Eigen::Vector3f corners[4] = { origin, origin + across, origin + across + down, origin + down };
			const Eigen::Vector2f uvs[4] = { { glyph->u0, glyph->v0 }, { glyph->u1, glyph->v0 },
				{ glyph->u1, glyph->v1 }, { glyph->u0, glyph->v1 } };
			const int indices[6] = { 0, 1, 2, 0, 2, 3 };
			for (int i = 0; i < 6; ++i)
			{
				textVertexArray.emplace_back(corners[indices[i]], p_pixelSize, p_color, uvs[indices[i]]);
			}
			textRuns.back().count += 6;
			penX += glyph->advanceX;
		}
	}

	void ImRenderer::drawText3D(const Eigen::Vector3f& p_position, const char* p_text,
		float p_pixelSize, Eigen::Vector4<uint8_t> p_color, bool p_overlay)
	{
		// Camera basis: columns of the inverse view are right/up/back in world space.
		const Eigen::Vector3f right(cameraParam.inverseView(0, 0), cameraParam.inverseView(1, 0), cameraParam.inverseView(2, 0));
		const Eigen::Vector3f up(cameraParam.inverseView(0, 1), cameraParam.inverseView(1, 1), cameraParam.inverseView(2, 1));
		const Eigen::Vector3f back(cameraParam.inverseView(0, 2), cameraParam.inverseView(1, 2), cameraParam.inverseView(2, 2));

		float worldPerPixel = 1.0f;
		const float projY = cameraParam.proj(1, 1) != 0.0f ? cameraParam.proj(1, 1) : 1.0f;
		if (cameraParam.orthProj)
		{
			worldPerPixel = (2.0f / projY) / std::max(1.0f, cameraParam.viewportHeight);
		}
		else
		{
			// Depth along the view direction decides how much world one pixel covers.
			const float depth = std::abs((p_position - cameraParam.eye).dot(back));
			worldPerPixel = (2.0f * depth / projY) / std::max(1.0f, cameraParam.viewportHeight);
		}
		pendingTextOverlay = p_overlay;
		drawText3D(p_position, p_text, p_pixelSize, p_color, right, up, worldPerPixel);
		pendingTextOverlay = false;
	}

	void ImRenderer::drawLabel3D(const Eigen::Vector3f& p_anchor, const char* p_text,
		float p_pixelSize, Eigen::Vector4<uint8_t> p_color, float p_offsetXPx, float p_offsetYPx,
		bool p_overlay)
	{
		const Eigen::Vector3f right(cameraParam.inverseView(0, 0), cameraParam.inverseView(1, 0), cameraParam.inverseView(2, 0));
		const Eigen::Vector3f up(cameraParam.inverseView(0, 1), cameraParam.inverseView(1, 1), cameraParam.inverseView(2, 1));
		const Eigen::Vector3f back(cameraParam.inverseView(0, 2), cameraParam.inverseView(1, 2), cameraParam.inverseView(2, 2));

		const float projY = cameraParam.proj(1, 1) != 0.0f ? cameraParam.proj(1, 1) : 1.0f;
		float worldPerPixel = 1.0f;
		if (cameraParam.orthProj)
		{
			worldPerPixel = (2.0f / projY) / std::max(1.0f, cameraParam.viewportHeight);
		}
		else
		{
			const float depth = std::abs((p_anchor - cameraParam.eye).dot(back));
			worldPerPixel = (2.0f * depth / projY) / std::max(1.0f, cameraParam.viewportHeight);
		}

		// The caller's offset is in pixels with y pointing down, the basis' up points up.
		const Eigen::Vector3f origin = p_anchor
			+ right * (p_offsetXPx * worldPerPixel)
			- up * (p_offsetYPx * worldPerPixel);
		drawText3D(origin, p_text, p_pixelSize, p_color, right, up, worldPerPixel);
	}

	void ImRenderer::drawLabel3D(const Eigen::Vector3f& p_anchor, const char* p_text,
		float p_pixelSize, Eigen::Vector4<uint8_t> p_color, float p_offsetXPx, float p_offsetYPx,
		const Eigen::Vector3f& p_basisRight, const Eigen::Vector3f& p_basisUp, bool p_overlay)
	{
		// Same pixel scale as the camera aligned version (so the label stays readable), but the
		// quads follow the caller's basis: a sketch annotation turns with its plane.
		const Eigen::Vector3f back(cameraParam.inverseView(0, 2), cameraParam.inverseView(1, 2), cameraParam.inverseView(2, 2));
		const float projY = cameraParam.proj(1, 1) != 0.0f ? cameraParam.proj(1, 1) : 1.0f;
		float worldPerPixel = 1.0f;
		if (cameraParam.orthProj)
		{
			worldPerPixel = (2.0f / projY) / std::max(1.0f, cameraParam.viewportHeight);
		}
		else
		{
			const float depth = std::abs((p_anchor - cameraParam.eye).dot(back));
			worldPerPixel = (2.0f * depth / projY) / std::max(1.0f, cameraParam.viewportHeight);
		}

		const Eigen::Vector3f right = p_basisRight.norm() > 0.0f ? p_basisRight.normalized() : Eigen::Vector3f(1, 0, 0);
		const Eigen::Vector3f up = p_basisUp.norm() > 0.0f ? p_basisUp.normalized() : Eigen::Vector3f(0, 1, 0);
		// Offsets are given in screen pixels with y pointing down, while the basis' up points up.
		const Eigen::Vector3f origin = p_anchor
			+ right * (p_offsetXPx * worldPerPixel)
			- up * (p_offsetYPx * worldPerPixel);
		pendingTextOverlay = p_overlay;
		drawText3D(origin, p_text, p_pixelSize, p_color, right, up, worldPerPixel);
		pendingTextOverlay = false;
	}

	void ImRenderer::drawIcon3D(const Eigen::Vector3f& p_position,
		Rendering::Resources::Texture* p_texture, float p_pixelSize,
		Eigen::Vector4<uint8_t> p_color, bool p_overlay)
	{
		if (p_texture == nullptr || p_pixelSize <= 0.0f)
		{
			return;
		}
		const Eigen::Vector3f right(cameraParam.inverseView(0, 0), cameraParam.inverseView(1, 0), cameraParam.inverseView(2, 0));
		const Eigen::Vector3f up(cameraParam.inverseView(0, 1), cameraParam.inverseView(1, 1), cameraParam.inverseView(2, 1));
		const Eigen::Vector3f back(cameraParam.inverseView(0, 2), cameraParam.inverseView(1, 2), cameraParam.inverseView(2, 2));

		const float projY = cameraParam.proj(1, 1) != 0.0f ? cameraParam.proj(1, 1) : 1.0f;
		float worldPerPixel = 1.0f;
		if (cameraParam.orthProj)
		{
			worldPerPixel = (2.0f / projY) / std::max(1.0f, cameraParam.viewportHeight);
		}
		else
		{
			const float depth = std::abs((p_position - cameraParam.eye).dot(back));
			worldPerPixel = (2.0f * depth / projY) / std::max(1.0f, cameraParam.viewportHeight);
		}

		const float half = p_pixelSize * 0.5f * worldPerPixel;
		const Eigen::Vector3f corners[4] = {
			p_position - right * half + up * half,
			p_position + right * half + up * half,
			p_position + right * half - up * half,
			p_position - right * half - up * half };
		const Eigen::Vector2f uvs[4] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f } };
		const int indices[6] = { 0, 1, 2, 0, 2, 3 };

		if (textRuns.empty() || textRuns.back().texture != p_texture)
		{
			textRuns.push_back({ static_cast<int>(textVertexArray.size()), 0, -1, p_overlay, p_texture });
		}
		for (int i = 0; i < 6; ++i)
		{
			textVertexArray.emplace_back(corners[indices[i]], p_pixelSize, p_color, uvs[indices[i]]);
		}
		textRuns.back().count += 6;
	}

	void ImRenderer::drawIcon3D(const Eigen::Vector3f& p_position,
		Rendering::Resources::Texture* p_texture, float p_pixelSize,
		Eigen::Vector4<uint8_t> p_color, const Eigen::Vector3f& p_basisRight,
		const Eigen::Vector3f& p_basisUp, float p_offsetXPx, float p_offsetYPx, bool p_overlay)
	{
		if (p_texture == nullptr || p_pixelSize <= 0.0f)
		{
			return;
		}
		const Eigen::Vector3f back(cameraParam.inverseView(0, 2), cameraParam.inverseView(1, 2), cameraParam.inverseView(2, 2));
		const float projY = cameraParam.proj(1, 1) != 0.0f ? cameraParam.proj(1, 1) : 1.0f;
		float worldPerPixel = 1.0f;
		if (cameraParam.orthProj)
		{
			worldPerPixel = (2.0f / projY) / std::max(1.0f, cameraParam.viewportHeight);
		}
		else
		{
			const float depth = std::abs((p_position - cameraParam.eye).dot(back));
			worldPerPixel = (2.0f * depth / projY) / std::max(1.0f, cameraParam.viewportHeight);
		}

		const Eigen::Vector3f right = p_basisRight.norm() > 0.0f ? p_basisRight.normalized() : Eigen::Vector3f(1, 0, 0);
		const Eigen::Vector3f up = p_basisUp.norm() > 0.0f ? p_basisUp.normalized() : Eigen::Vector3f(0, 1, 0);
		const Eigen::Vector3f centre = p_position
			+ right * (p_offsetXPx * worldPerPixel)
			- up * (p_offsetYPx * worldPerPixel);

		const float half = p_pixelSize * 0.5f * worldPerPixel;
		const Eigen::Vector3f corners[4] = {
			centre - right * half + up * half,
			centre + right * half + up * half,
			centre + right * half - up * half,
			centre - right * half - up * half };
		const Eigen::Vector2f uvs[4] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f } };
		const int indices[6] = { 0, 1, 2, 0, 2, 3 };

		if (textRuns.empty() || textRuns.back().texture != p_texture || textRuns.back().overlay != p_overlay)
		{
			textRuns.push_back({ static_cast<int>(textVertexArray.size()), 0, -1, p_overlay, p_texture });
		}
		for (int i = 0; i < 6; ++i)
		{
			textVertexArray.emplace_back(corners[indices[i]], p_pixelSize, p_color, uvs[indices[i]]);
		}
		textRuns.back().count += 6;
	}

	void ImRenderer::drawText()
	{
		if (!textVertexArray.empty() && mTextMaterial != nullptr)
		{
			auto& font = Rendering::Overlay::Im2DRender::Instance().GetFont();
			const Eigen::Matrix4f viewProjT = cameraParam.viewProj.transpose();

			auto* driver = Core::Global::ServiceLocator::Get<Editor::Core::Context>().driver.get();
			auto depthTested = driver->CreatePipelineState();
			depthTested.depthTest = true;
			depthTested.depthWriting = false;
			depthTested.blending = true;
			depthTested.blendingSrcFactor = Rendering::Settings::EBlendingFactor::SRC_ALPHA;
			depthTested.blendingDestFactor = Rendering::Settings::EBlendingFactor::ONE_MINUS_SRC_ALPHA;
			depthTested.culling = 0;
			auto alwaysOnTop = depthTested;
			alwaysOnTop.depthTest = false;
			bool currentOverlay = false;

			for (const TextRun& run : textRuns)
			{
				if (run.count <= 0)
				{
					continue;
				}

				// One run, one texture: the font atlas for text, the icon's own texture for an
				// image. This range is the draw command boundary, the way ImGui splits its list.
				Rendering::Resources::Texture* texture = run.texture;
				if (texture == nullptr && run.atlas >= 0 && run.atlas < font.GetAtlasCount())
				{
					if (static_cast<int>(textAtlasTextures.size()) <= run.atlas)
					{
						textAtlasTextures.resize(run.atlas + 1, nullptr);
					}
					texture = textAtlasTextures[run.atlas];
					const std::vector<uint8_t>& pixels = font.GetAtlasPixels(run.atlas);
					if (texture == nullptr)
					{
						texture = Rendering::Resources::Loaders::TextureLoader::CreateMutable(
							static_cast<uint32_t>(font.GetAtlasWidth(run.atlas)),
							static_cast<uint32_t>(font.GetAtlasHeight(run.atlas)),
							pixels.data(),
							Rendering::Settings::ETextureFilteringMode::LINEAR,
							Rendering::Settings::ETextureFilteringMode::LINEAR,
							Rendering::Settings::ETextureWrapMode::CLAMP_TO_EDGE,
							Rendering::Settings::ETextureWrapMode::CLAMP_TO_EDGE,
							false);
						textAtlasTextures[run.atlas] = texture;
					}
					else if (font.ConsumeAtlasDirty(run.atlas))
					{
						texture->GetTexture().Upload(pixels.data(),
							Rendering::Settings::EFormat::RGBA,
							Rendering::Settings::EPixelDataType::UNSIGNED_BYTE);
					}
				}
				if (texture == nullptr)
				{
					continue;
				}

				if (driver != nullptr && run.overlay != currentOverlay)
				{
					currentOverlay = run.overlay;
					driver->SetPipelineState(currentOverlay ? alwaysOnTop : depthTested);
				}
				mTextMaterial->SetProperty("uAtlas", texture);
				mTextMaterial->Bind(&mEmptyTexture2D, &mEmptyTextureCube);
				mTextMaterial->UploadProperties(true, &mEmptyTexture2D, &mEmptyTextureCube);
				m_overlayGl.Draw(*mTextMaterial, GL_TRIANGLES, false,
					textVertexArray.data() + run.first, run.count, sizeof(VertexFormat),
					viewProjT.data(), cameraParam.viewportWidth, cameraParam.viewportHeight);
				mTextMaterial->Unbind();
			}
		}
		textVertexArray.clear();
		textRuns.clear();
	}

	void ImRenderer::drawDashedLineList(const std::vector<Eigen::Vector3f>& p_position, float p_size,
		Eigen::Vector4<uint8_t> p_color, float p_dashPx, float p_gapPx, float p_phase)
	{
		if (p_position.size() < 2 || p_dashPx <= 0.0f)
		{
			return;
		}
		const float cycle = p_dashPx + std::max(0.0f, p_gapPx);
		std::vector<Eigen::Vector3f> dashes;
		dashes.reserve(p_position.size() * 4);
		float phase = std::fmod(std::max(0.0f, p_phase), cycle);
		for (size_t i = 0; i + 1 < p_position.size(); i += 2)
		{
			const Eigen::Vector3f& a = p_position[i];
			const Eigen::Vector3f& b = p_position[i + 1];
			const Eigen::Vector2f aScreen = worldToScreen(a);
			const Eigen::Vector2f bScreen = worldToScreen(b);
			const float length = (bScreen - aScreen).norm();
			// The projection can hand back a non finite or absurd screen length for points just
			// behind the camera: splitting that would spin for years, so those segments are skipped.
			if (!std::isfinite(length) || length > 20000.0f)
			{
				continue;
			}
			if (length < 0.05f)
			{
				continue;
			}
			const Eigen::Vector3f dir = b - a;
			float t = 0.0f;
			while (t < length - 0.1f)
			{
				const bool ink = phase < p_dashPx;
				const float remain = ink ? (p_dashPx - phase) : (cycle - phase);
				// A zero or negative remain (phase landing exactly on a boundary, or drifting past
				// it) would make step 0 and spin here forever: restart the cycle instead.
				if (remain <= 0.0f)
				{
					phase = 0.0f;
					continue;
				}
				const float step = std::min(remain, length - t);
				if (dashes.size() > 200000)
				{
					break;
				}
				if (ink && step > 0.1f)
				{
					dashes.push_back(a + dir * (t / length));
					dashes.push_back(a + dir * ((t + step) / length));
				}
				t += step;
				phase += step;
				if (phase >= cycle)
				{
					phase -= cycle;
				}
			}
		}
		if (!dashes.empty())
		{
			drawLineList(dashes, p_size, p_color);
		}
	}

	void ImRenderer::drawDashedPolyline(const std::vector<Eigen::Vector3f>& p_points, float p_size,
		Eigen::Vector4<uint8_t> p_color, float p_dashPx, float p_gapPx, float p_phase)
	{
		if (p_points.size() < 2)
		{
			return;
		}
		std::vector<Eigen::Vector3f> pairs;
		pairs.reserve((p_points.size() - 1) * 2);
		for (size_t i = 0; i + 1 < p_points.size(); ++i)
		{
			pairs.push_back(p_points[i]);
			pairs.push_back(p_points[i + 1]);
		}
		drawDashedLineList(pairs, p_size, p_color, p_dashPx, p_gapPx, p_phase);
	}

	void ImRenderer::drawDashedPolyline(const std::vector<Eigen::Vector3f>& p_points,
		float p_dashPx, float p_gapPx, float p_phase)
	{
		drawDashedPolyline(p_points, sizeStack.back(), colorStack.back(), p_dashPx, p_gapPx, p_phase);
	}

	void ImRenderer::drawLineList(const std::vector<Eigen::Vector3f>& position, float _size, Eigen::Vector4<uint8_t> _color)
	{
		std::vector<VertexFormat> vd(position.size());
		float alpha = alphaStack.back();
		if (matrixStack.size() > 1) // optim, skip the matrix multiplication when the stack size is 1
		{
			for (int i = 0; i < position.size(); i++)
			{
				vd[i] = { MatrixMulPoint(matrixStack.back(), position[i]), _size, _color };
				vd[i].color[0] *= alpha;
			}
		}
		else
		{
			for (int i = 0; i < position.size(); i++)
			{
				vd[i] = { position[i], _size, _color };
				vd[i].color[0] *= alpha;
			}
		}
		VertexList* vertexList = vertexData[vertexDataIndex][layerIndex * DrawPrimitiveCount + DrawPrimitiveLines];
		vertexList->insert(vertexList->end(), vd.begin(), vd.end());
	}

	void ImRenderer::drawTriangleList(const std::vector<Eigen::Vector3f>& position, float _size, Eigen::Vector4<uint8_t> _color)
	{
		std::vector<VertexFormat> vd(position.size());
		float alpha = alphaStack.back();
		if (matrixStack.size() > 1) // optim, skip the matrix multiplication when the stack size is 1
		{
			for (int i = 0; i < position.size(); i++)
			{
				vd[i] = { MatrixMulPoint(matrixStack.back(), position[i]), _size, _color };
				vd[i].color[0] *= alpha;
			}
		}
		else
		{
			for (int i = 0; i < position.size(); i++)
			{
				vd[i] = { position[i], _size, _color };
				vd[i].color[0] *= alpha;
			}
		}
		VertexList* vertexList = vertexData[vertexDataIndex][layerIndex * DrawPrimitiveCount + DrawPrimitiveTriangles];
		vertexList->insert(vertexList->end(), vd.begin(), vd.end());
	}

	void ImRenderer::drawTriangleList(const std::vector<Eigen::Vector3f>& position,
		const std::vector<Eigen::Vector3f>& normals, float _size, Eigen::Vector4<uint8_t> _color)
	{
		// 法线是构造几何时算好的（WidgetViewData::TriangleFace::RebuildNormals），这里只做
		// 矩阵变换和上传：绘制期不再求叉积。没有法线或光照关着时退回普通三角形通道。
		if (!enableLit || normals.size() < position.size() || position.empty())
		{
			drawTriangleList(position, _size, _color);
			return;
		}

		const bool transformed = matrixStack.size() > 1;
		const Eigen::Matrix4f& matrix = matrixStack.back();
		const float alpha = alphaStack.back();
		litVertexArray.reserve(litVertexArray.size() + position.size());
		for (size_t i = 0; i < position.size(); ++i)
		{
			Eigen::Vector3f p = transformed ? MatrixMulPoint(matrix, position[i]) : position[i];
			Eigen::Vector3f n = transformed ? MatrixMulDir(matrix, normals[i]) : normals[i];
			litVertexArray.push_back(VertexFormat(p, _color, n));
			litVertexArray.back().color[0] *= alpha;
		}
	}

	void ImRenderer::appendLitVertex(VertexFormat& vertex, const Eigen::Vector3f& position, const Eigen::Vector3f& normal)
	{
		// 与顶点桶里的 vertex() 保持一致：矩阵栈、全局 alpha 都要作用上，否则打开光照后
		// 同一个 widget 的带法线几何会和它不带法线的几何错位。
		if (matrixStack.size() > 1)
		{
			const Eigen::Vector3f pos = MatrixMulPoint(matrixStack.back(), position);
			vertex.positionSize = Eigen::Vector4<float>(pos.x(), pos.y(), pos.z(), vertex.positionSize.w());
			vertex.normal = MatrixMulDir(matrixStack.back(), normal);
		}
		vertex.color(0) = vertex.color(0) * alphaStack.back();
	}

	void ImRenderer::drawLine(
		const Eigen::Vector3f& _a, const Eigen::Vector3f& _b, float _size, Eigen::Vector4<uint8_t> _color)
	{
		begin(PrimitiveModeLines);
		vertex(_a, _size, _color);
		vertex(_b, _size, _color);
		end();
	}

	void ImRenderer::drawLine(const Eigen::Vector3f& _a, const Eigen::Vector3f& _b, float _size)
	{
		drawLine(_a, _b, _size, colorStack.back());
	}

	void ImRenderer::drawLine(const Eigen::Vector3f& _a, const Eigen::Vector3f& _b)
	{
		drawLine(_a, _b, sizeStack.back());
	}



	void ImRenderer::drawTriangle(const Eigen::Vector3f& a, const Eigen::Vector3f& b, const Eigen::Vector3f& c, const Eigen::Vector3f& n)
	{
		auto& matrix = matrixStack.back();
		Eigen::Vector3f mn = MatrixMulDir(matrix, n);
		litVertexArray.push_back({ MatrixMulPoint(matrix, a), colorStack.back(), mn });
		litVertexArray.push_back({ MatrixMulPoint(matrix, b), colorStack.back(), mn });
		litVertexArray.push_back({ MatrixMulPoint(matrix, c), colorStack.back(), mn });
	}

	void ImRenderer::drawTriangle(const Eigen::Vector3f& _a, const Eigen::Vector3f& _b, const Eigen::Vector3f& _c)
	{
		drawTriangle(_a, _b, _c, colorStack.back());
	}

	void ImRenderer::drawTriangle(const Eigen::Vector3f& _a, const Eigen::Vector3f& _b, const Eigen::Vector3f& _c,
		const Eigen::Vector4<uint8_t>& _color)
	{
		colorStack.push_back(_color);
		begin(PrimitiveModeTriangles);
		vertex(_a);
		vertex(_b);
		vertex(_c);
		end();
		colorStack.pop_back();
	}


	void ImRenderer::drawQuadFilled(
		const Eigen::Vector3f& _origin, const Eigen::Vector3f& _normal, const Eigen::Vector2<float>& _size)
	{
		Eigen::Matrix4f matrix = matrixStack.back() * LookAt(_origin, _origin + _normal);
		matrixStack.push_back(matrix);
		drawQuadFilled(Eigen::Vector3f(-_size.x(), -_size.y(), 0.0f),
			Eigen::Vector3f(_size.x(), -_size.y(), 0.0f), Eigen::Vector3f(_size.x(), _size.y(), 0.0f),
			Eigen::Vector3f(-_size.x(), _size.y(), 0.0f));
		matrixStack.pop_back();
	}

	void ImRenderer::drawQuad(const Eigen::Vector3f& _a, const Eigen::Vector3f& _b, const Eigen::Vector3f& _c,
		const Eigen::Vector3f& _d)
	{
		begin(PrimitiveModeLineLoop);
		vertex(_a);
		vertex(_b);
		vertex(_c);
		vertex(_d);
		end();
	}

	void ImRenderer::drawQuad(
		const Eigen::Vector3f& _origin, const Eigen::Vector3f& _normal, const Eigen::Vector2<float>& _size)
	{
		matrixStack.push_back(matrixStack.back() * LookAt(_origin, _origin + _normal));
		drawQuad(Eigen::Vector3f(-_size.x(), _size.y(), 0.0f), Eigen::Vector3f(_size.x(), _size.y(), 0.0f),
			Eigen::Vector3f(_size.x(), -_size.y(), 0.0f), Eigen::Vector3f(-_size.x(), -_size.y(), 0.0f));
		matrixStack.pop_back();
	}

	void ImRenderer::drawQuadFilled(const Eigen::Vector3f& _a, const Eigen::Vector3f& _b,
		const Eigen::Vector3f& _c, const Eigen::Vector3f& _d)
	{

		begin(PrimitiveModeTriangles);
		vertex(_a);
		vertex(_b);
		vertex(_c);
		vertex(_a);
		vertex(_c);
		vertex(_d);
		end();
	}
	void ImRenderer::drawArrow(const Eigen::Vector3f& _start, const Eigen::Vector3f& _end, float _headLength,
		float axisThickness, float _headThickness)
	{
		sizeStack.push_back(axisThickness);
		drawArrow(_start, _end, _headLength, _headThickness);
		sizeStack.pop_back();
	}
	void ImRenderer::drawArrow(
		const Eigen::Vector3f& _start, const Eigen::Vector3f& _end, float _headLength, float _headThickness)
	{

		if (_headThickness < 0.0f)
		{
			_headThickness = sizeStack.back() * 2.0f;
		}
		Eigen::Vector3f dir = _end - _start;
		float dirlen = dir.norm();
		if (_headLength < 0.0f)
		{
			_headLength = std::min(dirlen / 2.0f, pixelsToWorldSize(_end, _headThickness * 2.0f));
		}
		dir = dir / dirlen;

		Eigen::Vector3f head = _end - dir * _headLength;

		begin(PrimitiveModeLines);
		vertex(_start);
		vertex(head);
		vertex(head, _headThickness, colorStack.back());
		vertex(_end, 2.0f, colorStack.back()); // \hack \todo 2.0f here compensates for the shader antialiasing (which
		// reduces alpha when size < 2)
		end();
	}

	void ImRenderer::drawCircleFilled(
		const Eigen::Vector3f& _origin, const Eigen::Vector3f& _normal, float _radius, int _detail)
	{
		if (_detail < 0)
		{
			_detail = estimateLevelOfDetail(_origin, _radius, 8, 64);
		}
		_detail = std::max(_detail, 3);

		matrixStack.push_back(matrixStack.back() * LookAt(_origin, _origin + _normal));

		begin(PrimitiveModeTriangles);
		float cp = _radius;
		float sp = 0.0f;
		for (int i = 1; i <= _detail; ++i)
		{
			vertex(Eigen::Vector3f(0.0f, 0.0f, 0.0f));
			vertex(Eigen::Vector3f(cp, sp, 0.0f));
			float rad = TwoPi * ((float)i / (float)_detail);
			float c = cosf(rad) * _radius;
			float s = sinf(rad) * _radius;
			vertex(Eigen::Vector3f(c, s, 0.0f));
			cp = c;
			sp = s;
		}
		end();
		matrixStack.pop_back();
	}

	void ImRenderer::drawTransparencyCircle(const Eigen::Vector3f& _origin, const Eigen::Vector3f& _normal, float _radius, int _detail)
	{
		alphaStack.push_back(0.5);
		colorStack.push_back(Color_Green);
		pushEnableSorting(true);
		drawCircleFilled(_origin, _normal, _radius, _detail);
		popEnableSorting();
		alphaStack.pop_back();
		colorStack.pop_back();
		sizeStack.push_back(3.0);
		colorStack.push_back(Color_Green);
		drawCircle(_origin, _normal, _radius, _detail);
		sizeStack.pop_back();
		colorStack.pop_back();
	}

	void ImRenderer::drawSphere(const Eigen::Vector3f& _origin, float _radius, int _detail)
	{
		if (_detail < 0)
		{
			_detail = estimateLevelOfDetail(_origin, _radius, 12, 32);
		}
		_detail = std::max(_detail, 6);
		// xy circle
		begin(PrimitiveModeLineLoop);
		for (int i = 0; i < _detail; ++i)
		{
			float rad = TwoPi * ((float)i / (float)_detail);
			vertex(Eigen::Vector3f(cosf(rad) * _radius + _origin.x(), sinf(rad) * _radius + _origin.y(), 0.0f + _origin.z()));
		}
		end();
		// xz circle
		begin(PrimitiveModeLineLoop);
		for (int i = 0; i < _detail; ++i)
		{
			float rad = TwoPi * ((float)i / (float)_detail);
			vertex(
				Eigen::Vector3f(cosf(rad) * _radius + _origin.x(), 0.0f + _origin.y(), sinf(rad) * _radius + _origin.z()));
		}
		end();
		// yz circle
		begin(PrimitiveModeLineLoop);
		for (int i = 0; i < _detail; ++i)
		{
			float rad = TwoPi * ((float)i / (float)_detail);
			vertex(Eigen::Vector3f(0.0f + _origin.x(), cosf(rad) * _radius + _origin.y(), sinf(rad) * _radius + _origin.z()));
		}
		end();

	}

	void ImRenderer::drawSphereFilled(const Eigen::Vector3f& _origin, float _radius, int _detail)
	{
		if (_detail < 0)
		{
			_detail = estimateLevelOfDetail(_origin, _radius, 12, 32);
		}
		_detail = std::max(_detail, 6);

		begin(PrimitiveModeTriangles);
		float yp = -_radius;
		float rp = 0.0f;
		auto& c = colorStack.back();
		for (int i = 1; i <= _detail / 2; ++i)
		{
			float y = ((float)i / (float)(_detail / 2)) * 2.0f - 1.0f;
			float r = cosf(y * HalfPi) * _radius;
			y = sinf(y * HalfPi) * _radius;
			float u = (float)i / (float)(_detail);
			float xp = 1.0f;
			float zp = 0.0f;
			for (int j = 1; j <= _detail; ++j)
			{
				float x = ((float)j / (float)(_detail)) * TwoPi;
				float z = sinf(x);
				float v = (float)j / (float)(_detail);
				x = cosf(x);
				if (enableLit)
				{
					vertex(Eigen::Vector3f(xp * rp + _origin.x(), yp + _origin.y(), zp * rp + _origin.z()), c, Eigen::Vector3f(xp * rp, yp, zp * rp).normalized(), { u, v });
					vertex(Eigen::Vector3f(xp * r + _origin.x(), y + _origin.y(), zp * r + _origin.z()), c, Eigen::Vector3f(xp * r, y, zp * r).normalized(), { u,v });
					vertex(Eigen::Vector3f(x * r + _origin.x(), y + _origin.y(), z * r + _origin.z()), c, Eigen::Vector3f(x * r, y, z * r).normalized(), { u, v });

					vertex(Eigen::Vector3f(xp * rp + _origin.x(), yp + _origin.y(), zp * rp + _origin.z()), c, Eigen::Vector3f(xp * rp, yp, zp * rp).normalized(), { u, v });
					vertex(Eigen::Vector3f(x * r + _origin.x(), y + _origin.y(), z * r + _origin.z()), c, Eigen::Vector3f(x * r, y, z * r).normalized(), { u, v });
					vertex(Eigen::Vector3f(x * rp + _origin.x(), yp + _origin.y(), z * rp + _origin.z()), c, Eigen::Vector3f(x * rp, yp, z * rp).normalized(), { u, v });
				}
				else
				{
					vertex(Eigen::Vector3f(xp * rp + _origin.x(), yp + _origin.y(), zp * rp + _origin.z()));
					vertex(Eigen::Vector3f(xp * r + _origin.x(), y + _origin.y(), zp * r + _origin.z()));
					vertex(Eigen::Vector3f(x * r + _origin.x(), y + _origin.y(), z * r + _origin.z()));

					vertex(Eigen::Vector3f(xp * rp + _origin.x(), yp + _origin.y(), zp * rp + _origin.z()));
					vertex(Eigen::Vector3f(x * r + _origin.x(), y + _origin.y(), z * r + _origin.z()));
					vertex(Eigen::Vector3f(x * rp + _origin.x(), yp + _origin.y(), z * rp + _origin.z()));
				}
				xp = x;
				zp = z;
			}
			yp = y;
			rp = r;
		}
		end();
	}

	void ImRenderer::drawCircleFaceCamera(const Eigen::Vector3f& _origin)
	{
		sizeStack.push_back(2.8);
		drawCircle(_origin, cameraParam.viewDirectioin, pixelsToWorldSize(_origin, 7.5), 40);
		sizeStack.pop_back();
	}

	void ImRenderer::drawCircle(const Eigen::Vector3f& _origin, const Eigen::Vector3f& _normal, float _radius, int _detail)
	{
		if (_detail < 0)
		{
			_detail = estimateLevelOfDetail(_origin, _radius, 8, 48);
		}
		_detail = std::max(_detail, 3);
		matrixStack.push_back(matrixStack.back() * LookAt(_origin, _origin + _normal));
		begin(PrimitiveModeLineLoop);
		for (int i = 0; i < _detail; ++i)
		{
			float rad = TwoPi * ((float)i / (float)_detail);
			vertex(Eigen::Vector3f(cosf(rad) * _radius, sinf(rad) * _radius, 0.0f));
		}
		end();
		matrixStack.pop_back();
	}
	void ImRenderer::drawConeFilled(const Eigen::Vector3f& _origin, const Eigen::Vector3f& _normal, float height,
		float _radius, int _detail)
	{
		if (_detail < 0)
		{
			_detail = estimateLevelOfDetail(_origin + _normal * height / 2, height / 2, 8, 48);
		}
		_detail = std::max(_detail, 3);
		// cone bottom face
		drawCircleFilled(_origin, _normal, _radius, _detail);
		// cone side face
		matrixStack.push_back(matrixStack.back() * LookAt(_origin, _origin + _normal));
		begin(PrimitiveModeTriangles);
		float cp = _radius;
		float sp = 0.0f;
		for (int i = 1; i <= _detail; ++i)
		{
			vertex(Eigen::Vector3f(0, 0, 1) * height);
			vertex(Eigen::Vector3f(cp, sp, 0.0f));
			float rad = TwoPi * ((float)i / (float)_detail);
			float c = cosf(rad) * _radius;
			float s = sinf(rad) * _radius;
			vertex(Eigen::Vector3f(c, s, 0.0f));
			cp = c;
			sp = s;
		}
		end();
		matrixStack.pop_back();
	}

	void ImRenderer::drawAlignedBox(const Eigen::Vector3f& _min, const Eigen::Vector3f& _max)
	{
		begin(PrimitiveModeLineLoop);
		vertex(Eigen::Vector3f(_min.x(), _min.y(), _min.z()));
		vertex(Eigen::Vector3f(_max.x(), _min.y(), _min.z()));
		vertex(Eigen::Vector3f(_max.x(), _min.y(), _max.z()));
		vertex(Eigen::Vector3f(_min.x(), _min.y(), _max.z()));
		end();
		begin(PrimitiveModeLineLoop);
		vertex(Eigen::Vector3f(_min.x(), _max.y(), _min.z()));
		vertex(Eigen::Vector3f(_max.x(), _max.y(), _min.z()));
		vertex(Eigen::Vector3f(_max.x(), _max.y(), _max.z()));
		vertex(Eigen::Vector3f(_min.x(), _max.y(), _max.z()));
		end();
		begin(PrimitiveModeLines);
		vertex(Eigen::Vector3f(_min.x(), _min.y(), _min.z()));
		vertex(Eigen::Vector3f(_min.x(), _max.y(), _min.z()));
		vertex(Eigen::Vector3f(_max.x(), _min.y(), _min.z()));
		vertex(Eigen::Vector3f(_max.x(), _max.y(), _min.z()));
		vertex(Eigen::Vector3f(_min.x(), _min.y(), _max.z()));
		vertex(Eigen::Vector3f(_min.x(), _max.y(), _max.z()));
		vertex(Eigen::Vector3f(_max.x(), _min.y(), _max.z()));
		vertex(Eigen::Vector3f(_max.x(), _max.y(), _max.z()));
		end();
	}

	void ImRenderer::drawAlignedBoxFilled(const Eigen::Vector3f& _min, const Eigen::Vector3f& _max)
	{
		// x+
		drawQuadFilled(Eigen::Vector3f(_max.x(), _max.y(), _min.z()),
			Eigen::Vector3f(_max.x(), _max.y(), _max.z()), Eigen::Vector3f(_max.x(), _min.y(), _max.z()),
			Eigen::Vector3f(_max.x(), _min.y(), _min.z()));
		// x-
		drawQuadFilled(Eigen::Vector3f(_min.x(), _min.y(), _min.z()),
			Eigen::Vector3f(_min.x(), _min.y(), _max.z()), Eigen::Vector3f(_min.x(), _max.y(), _max.z()),
			Eigen::Vector3f(_min.x(), _max.y(), _min.z()));
		// y+
		drawQuadFilled(Eigen::Vector3f(_min.x(), _max.y(), _min.z()),
			Eigen::Vector3f(_min.x(), _max.y(), _max.z()), Eigen::Vector3f(_max.x(), _max.y(), _max.z()),
			Eigen::Vector3f(_max.x(), _max.y(), _min.z()));
		// y-
		drawQuadFilled(Eigen::Vector3f(_max.x(), _min.y(), _min.z()),
			Eigen::Vector3f(_max.x(), _min.y(), _max.z()), Eigen::Vector3f(_min.x(), _min.y(), _max.z()),
			Eigen::Vector3f(_min.x(), _min.y(), _min.z()));
		// z+
		drawQuadFilled(Eigen::Vector3f(_max.x(), _min.y(), _max.z()),
			Eigen::Vector3f(_max.x(), _max.y(), _max.z()), Eigen::Vector3f(_min.x(), _max.y(), _max.z()),
			Eigen::Vector3f(_min.x(), _min.y(), _max.z()));
		// z-
		drawQuadFilled(Eigen::Vector3f(_min.x(), _min.y(), _min.z()),
			Eigen::Vector3f(_min.x(), _max.y(), _min.z()), Eigen::Vector3f(_max.x(), _max.y(), _min.z()),
			Eigen::Vector3f(_max.x(), _min.y(), _min.z()));
	}

	void ImRenderer::drawAlignedBoxFilled(
		const Eigen::Vector3f& _min, const Eigen::Vector3f& _max, const std::vector<Eigen::Vector4<uint8_t>>& color)
	{
		if (color.size() == 0)
		{
			drawAlignedBoxFilled(_min, _max);
		}
		else if (color.size() == 1)
		{
			colorStack.push_back(color[0]);
			drawAlignedBoxFilled(_min, _max);
			colorStack.pop_back();
		}
		else if (color.size() == 6)
		{
			colorStack.push_back(color[0]);
			// x+
			drawQuadFilled(Eigen::Vector3f(_max.x(), _max.y(), _min.z()), Eigen::Vector3f(_max.x(), _max.y(), _max.z()),
				Eigen::Vector3f(_max.x(), _min.y(), _max.z()), Eigen::Vector3f(_max.x(), _min.y(), _min.z()));
			colorStack.pop_back();
			colorStack.push_back(color[1]);
			// x-
			drawQuadFilled(Eigen::Vector3f(_min.x(), _min.y(), _min.z()), Eigen::Vector3f(_min.x(), _min.y(), _max.z()),
				Eigen::Vector3f(_min.x(), _max.y(), _max.z()), Eigen::Vector3f(_min.x(), _max.y(), _min.z()));
			colorStack.pop_back();
			colorStack.push_back(color[2]);
			// y+
			drawQuadFilled(Eigen::Vector3f(_min.x(), _max.y(), _min.z()), Eigen::Vector3f(_min.x(), _max.y(), _max.z()),
				Eigen::Vector3f(_max.x(), _max.y(), _max.z()), Eigen::Vector3f(_max.x(), _max.y(), _min.z()));
			colorStack.pop_back();

			colorStack.push_back(color[3]);
			// y-
			drawQuadFilled(Eigen::Vector3f(_max.x(), _min.y(), _min.z()), Eigen::Vector3f(_max.x(), _min.y(), _max.z()),
				Eigen::Vector3f(_min.x(), _min.y(), _max.z()), Eigen::Vector3f(_min.x(), _min.y(), _min.z()));
			colorStack.pop_back();
			colorStack.push_back(color[4]);
			// z+
			drawQuadFilled(Eigen::Vector3f(_max.x(), _min.y(), _max.z()), Eigen::Vector3f(_max.x(), _max.y(), _max.z()),
				Eigen::Vector3f(_min.x(), _max.y(), _max.z()), Eigen::Vector3f(_min.x(), _min.y(), _max.z()));
			colorStack.pop_back();
			colorStack.push_back(color[5]);
			// z-
			drawQuadFilled(Eigen::Vector3f(_min.x(), _min.y(), _min.z()), Eigen::Vector3f(_min.x(), _max.y(), _min.z()),
				Eigen::Vector3f(_max.x(), _max.y(), _min.z()), Eigen::Vector3f(_max.x(), _min.y(), _min.z()));
			colorStack.pop_back();
		}
	}

	void ImRenderer::drawPlaneGrid(const Eigen::Vector3f& origin, const Eigen::Vector3f& normal, float scale, float gridSize)
	{
		// Draw plane grid

		const float gridHalf = (float)gridSize * 0.5f;
		Eigen::Vector3f up = abs(normal.y()) > 0.99 ? Eigen::Vector3f(1, 0, 0) : Eigen::Vector3f(0, 1, 0);
		Eigen::Vector3f xaxis = normal.cross(up).normalized();
		Eigen::Vector3f zaxis = xaxis.cross(normal).normalized();

		alphaStack.push_back(1.0f);
		sizeStack.push_back(3.0f);
		pushEnableSorting(true);
		begin(PrimitiveModeLines);
		for (int x = 0; x <= gridSize * scale; ++x)
		{
			float v = x * 1.0 / (scale);
			vertex(origin + xaxis * (gridHalf)+zaxis * (v - gridHalf), Eigen::Vector4<uint8_t>(255, 0, 0, 0));
			vertex(origin - xaxis * (gridHalf)+zaxis * (v - gridHalf), Color_Red);
		}
		for (int z = 0; z <= gridSize * scale; ++z)
		{
			float v = z * 1.0 / (scale);
			vertex(origin + zaxis * gridHalf + xaxis * (v - gridHalf), Eigen::Vector4<uint8_t>(255, 0, 0, 0));
			vertex(origin - zaxis * gridHalf + xaxis * (v - gridHalf), Color_Blue);
		}
		end();
		alphaStack.pop_back();
		sizeStack.pop_back();
		popEnableSorting();
	}

	int ImRenderer::estimateLevelOfDetail(const Eigen::Vector3f& _position, float _worldSize, int _min, int _max)
	{
		if (cameraParam.orthProj)
		{
			return _max;
		}

		float d = (_position - cameraParam.eye).norm();
		float x = Clamp(2.0f * atanf(_worldSize / (2.0f * d)), 0.0f, 1.0f);
		float fmin = (float)_min;
		float fmax = (float)_max;

		return (int)(fmin + (fmax - fmin) * x);
	}

	Eigen::Vector2f ImRenderer::worldToScreen(const Eigen::Vector3f& pos)
	{
		Eigen::Vector3f p=MatrixMulPoint(cameraParam.viewProj,pos);
		return Eigen::Vector2f((p.x()+1.0)/2.0*cameraParam.viewportWidth, (1.0-p.y()) / 2.0*cameraParam.viewportHeight);
	}
	FrameParam& ImRenderer::getFrameParam()
	{
		return cameraParam;
	}

	void ImRenderer::newFrame(Editor::Panels::SceneView* sceneView)
	{
		litVertexArray.clear();
		drawMeshList.clear();
		renderView = const_cast<Editor::Panels::SceneView*>(sceneView);
		// all state stacks should be default here.
		assert(colorStack.size() == 1);
		assert(alphaStack.size() == 1);
		assert(sizeStack.size() == 1);
		assert(enableSortingStack.size() == 1);
		assert(enableLitStack.size() == 1);
		assert(layerIdStack.size() == 1);
		assert(matrixStack.size() == 1);
		assert(idStack.size() == 1);
		assert(primMode == PrimitiveModeNone);
		primMode = PrimitiveModeNone;
		primType = DrawPrimitiveCount;
		assert(vertexData[0].size() == vertexData[1].size());
		for (int i = 0; i < vertexData[0].size(); ++i)
		{
			vertexData[0][i]->clear();
			vertexData[1][i]->clear();
		}
		drawLists.clear();
		sortCalled = false;
		endFrameCalled = false;

		// copy keydown array internally so that we can make a delta to detect key presses
		memcpy(keyDownPrev, keyDownCurr, KeyCount); // \todo avoid this copy, use an index

		// 平移/旋转/局部坐标系这几个模式键不再由渲染器处理：那是变换操纵器的语义，
		// 由 TransformGizmo 在进入交互时自己读 wasKeyPressed() 同步。

		// set up engine params
		auto& inputState = renderView->getInutState();
		auto& viewMatrix = renderView->GetCamera()->GetViewMatrix();
		Eigen::Matrix4f view;
		memcpy(view.data(), viewMatrix.data, 16 * sizeof(float));
		view.transposeInPlace();
		auto viewinverse = view.inverse();
		auto& projMat = renderView->GetCamera()->GetProjectionMatrix();
		Eigen::Matrix4f proj;
		memcpy(proj.data(), projMat.data, 16 * sizeof(float));
		proj.transposeInPlace();
		Eigen::Vector3f vieweye = Eigen::Vector3f(viewinverse(0, 3), viewinverse(1, 3), viewinverse(2, 3));
		Eigen::Vector3f viewforward = Eigen::Vector3f(viewinverse(0, 2), viewinverse(1, 2), viewinverse(2, 2)).normalized();
		Eigen::Matrix4f camWorld = viewinverse;

		auto [w, h] = renderView->GetSafeSize();
		auto [x, y] = inputState.GetMousePosition();
		Eigen::Vector2<float> cursorPos = Eigen::Vector2<float>(x, y);
		cursorPos = Eigen::Vector2<float>((cursorPos.x() / w) * 2.0f - 1.0f, (cursorPos.y() / h) * 2.0f - 1.0f);
		cursorPos(1) = -cursorPos(1);
		Eigen::Vector3f rayOrigin, rayDirection;
		//
		bool isOrth = renderView->GetCamera()->GetProjectionMode() == Rendering::Settings::EProjectionMode::ORTHOGRAPHIC;

		if (isOrth)
		{
			rayOrigin(0) = cursorPos.x() / proj(0, 0) - proj(0, 3) / proj(0, 0);
			rayOrigin(1) = cursorPos.y() / proj(1, 1) - proj(1, 3) / proj(1, 1);
			rayOrigin(2) = 0.0f;
			Eigen::Vector4<float> it = camWorld * Eigen::Vector4<float>(rayOrigin.x(), rayOrigin.y(), rayOrigin.z(), 1.0f);
			rayOrigin = Eigen::Vector3f(it.x() / it.w(), it.y() / it.w(), it.z() / it.w());
			it = camWorld * Eigen::Vector4<float>(0.0f, 0.0f, -1.0f, 0.0f);
			rayDirection = Eigen::Vector3f(it.x(), it.y(), it.z());
		}
		else
		{
			rayOrigin = vieweye;
			rayDirection(0) = cursorPos.x() / proj(0, 0);
			rayDirection(1) = cursorPos.y() / proj(1, 1);
			rayDirection(2) = -1.0f;
			Eigen::Vector3f rayDirectionNormalized = rayDirection.normalized();
			Eigen::Vector4<float> it = camWorld * Eigen::Vector4<float>(rayDirectionNormalized.x(),
				rayDirectionNormalized.y(), rayDirectionNormalized.z(), 0.0f);
			rayDirection = Eigen::Vector3f(it.x(), it.y(), it.z());
		}
		
		cameraParam.viewportWidth = w;
		cameraParam.viewportHeight = h;
		///
		cameraParam.projectY = 2.0 / proj(1, 1);
		cameraParam.orthProj = isOrth;
		cameraParam.eye = vieweye;
		/// for Orth
		cameraParam.viewDirectioin = viewforward;
		cameraParam.rayOrigin = rayOrigin;
		cameraParam.rayDirection = rayDirection;
		cameraParam.cursor.x() = x;
		cameraParam.cursor.y() = y;
		cameraParam.viewPortCursor.x() = x;
		cameraParam.viewPortCursor.y() = h - y;
		cameraParam.view = view;
		cameraParam.inverseView = view.inverse();
		cameraParam.proj = proj;
		cameraParam.viewProj = proj * view;

		bool ctrlDown = inputState.IsKeyDown(Editor::Panels::Control);

		cameraParam.keyDown[MouseLeft] = inputState.IsMouseButtonDown(Editor::Panels::MOUSE_BUTTON_LEFT);
		cameraParam.keyDown[MouseMiddle] = inputState.IsMouseButtonDown(Editor::Panels::MOUSE_BUTTON_MIDDLE);
		cameraParam.keyDown[MouseRight] = inputState.IsMouseButtonDown(Editor::Panels::MOUSE_BUTTON_RIGHT);
		cameraParam.keyDown[ActionControl] = ctrlDown;

		cameraParam.keyDown[KeyL] = ctrlDown && inputState.IsKeyDown(Editor::Panels::KEYL);

		cameraParam.keyDown[KeyT] = ctrlDown && inputState.IsKeyDown(Editor::Panels::KEYT);
		cameraParam.keyDown[KeyR] = ctrlDown && inputState.IsKeyDown(Editor::Panels::KEYR);
		cameraParam.keyDown[KeyS] = ctrlDown && inputState.IsKeyDown(Editor::Panels::KEYS);

		cameraParam.snapTranslation = 0.0f;
		cameraParam.snapRotation = 0.0f;
		cameraParam.snapScale = 0.0f;
		memcpy(keyDownCurr, cameraParam.keyDown, KeyCount); // must copy in case m_keyDown is updated after reset (e.g. by an app callback)

		// The frame the effects build their geometry against: the camera is final at this point,
		// so a submit method called later this frame can project its points itself.
		ImEffectContext effectContext;
		effectContext.viewProj = cameraParam.viewProj;
		effectContext.viewProjT = cameraParam.viewProj.transpose();
		effectContext.viewportWidth = cameraParam.viewportWidth;
		effectContext.viewportHeight = cameraParam.viewportHeight;
		for (const std::unique_ptr<AImEffect>& effect : mEffects)
		{
			effect->BeginFrame(effectContext);
		}
	}

	void ImRenderer::test()
	{
	}

	void ImRenderer::drawUnsort()
	{
		auto* driver = Core::Global::ServiceLocator::Get<Editor::Core::Context>().driver.get();

		drawLists.clear();
		// draw unsorted primitives first
		for (int i = 0; i < vertexData[0].size(); ++i)
		{
			if (vertexData[0][i]->size() > 0)
			{
				drawLists.push_back(DrawList());
				DrawList& dl = drawLists.back();
				dl.layerId = layerIdMap[i / DrawPrimitiveCount];
				dl.primType = (DrawPrimitiveType)(i % DrawPrimitiveCount);
				dl.vertexData = vertexData[0][i]->data();
				dl.vertexCount = vertexData[0][i]->size();
			}
		}
		// The overlay's pipeline state, viewport and point-sprite setup are the render module's
		// (Rendering::Overlay::Im3DGLRenderer); this function keeps building the geometry.
		m_overlayGl.Begin(*driver, cameraParam.viewportWidth, cameraParam.viewportHeight, true);

		for (int i = 0, n = drawLists.size(); i < n; ++i)
		{
			const DrawList& drawList = drawLists[i];
			unsigned int prim;
			Rendering::Data::Material* mat = nullptr;
			std::string passTag = "";
			switch (drawList.primType)
			{
			case DrawPrimitivePoints:
				prim = GL_POINTS;
				passTag = "point";
				mat = mPointMaterial;
				break;
			case DrawPrimitiveLines:
				prim = GL_LINES;
				passTag = "line";
				mat = mLineMaterial;
				break;
			case DrawPrimitiveTriangles:
				prim = GL_TRIANGLES;
				passTag = "triangle";
				mat = mTriangleMaterial;
				break;
			default:
				assert(false);
				return;
			};

			const Eigen::Matrix4f viewProjT = cameraParam.viewProj.transpose();
			m_overlayGl.Draw(*mat, prim, drawList.primType == DrawPrimitiveLines,
				drawList.vertexData, drawList.vertexCount, sizeof(VertexFormat),
				viewProjT.data(), cameraParam.viewportWidth, cameraParam.viewportHeight);
		}

		// Custom effects of this channel run after the built-in primitives, with the same state.
		drawEffects(EImChannel::Overlay);
	}

	void ImRenderer::drawSort()
	{
		drawLists.clear();
		auto driver = Core::Global::ServiceLocator::Get<Editor::Core::Context>().driver.get();
		auto p_pso = driver->CreatePipelineState();
		p_pso.stencilTest = true;
		p_pso.stencilWriteMask = 0xFF;
		p_pso.stencilFuncRef = 1;
		p_pso.stencilFuncMask = 0xFF;
		p_pso.stencilOpFail = Rendering::Settings::EOperation::REPLACE;
		p_pso.depthOpFail = Rendering::Settings::EOperation::REPLACE;
		p_pso.bothOpFail = Rendering::Settings::EOperation::REPLACE;
		p_pso.colorWriting.mask = 0x00;
		p_pso.depthWriting = true;
		p_pso.colorWriting.mask = 0xFF;
		p_pso.blending = true;
		p_pso.blendingEquation = Rendering::Settings::EBlendingEquation::FUNC_ADD;
		
		p_pso.blendingSrcFactor = Rendering::Settings::EBlendingFactor::SRC_ALPHA;
		p_pso.culling = 0;
		p_pso.cullFace = Rendering::Settings::ECullFace::FRONT_AND_BACK;
		p_pso.blendingDestFactor = Rendering::Settings::EBlendingFactor::ONE_MINUS_SRC_ALPHA;
		p_pso.depthTest = true;
		p_pso.depthFunc = Rendering::Settings::EComparaisonAlgorithm::GREATER_EQUAL;
		driver->SetPipelineState(p_pso);

		// Backdrop effects first: they are what the axes and the geometry below are drawn on top
		// of (the sketch grid is one), and they blend, so the order is what keeps them behind.
		drawEffects(EImChannel::Backdrop);

		// Enable Depth Test
		for (int i = 0; i < vertexData[1].size(); ++i)
		{
			if (vertexData[1][i]->size() > 0)
			{
				drawLists.push_back(DrawList());
				DrawList& dl = drawLists.back();
				dl.layerId = layerIdMap[i / DrawPrimitiveCount];
				dl.primType = (DrawPrimitiveType)(i % DrawPrimitiveCount);
				dl.vertexData = vertexData[1][i]->data();
				dl.vertexCount = vertexData[1][i]->size();
			}
		}
		for (int i = 0, n = drawLists.size(); i < n; ++i)
		{
			const DrawList& drawList = drawLists[i];
			unsigned int prim;

			
			Rendering::Data::Material* mat = nullptr;
			switch (drawList.primType)
			{
			case DrawPrimitivePoints:
				prim = GL_POINTS;
				mat = mPointMaterial;
			
				break;
			case DrawPrimitiveLines:
				prim = GL_LINES;
				mat = mLineMaterial;
				break;
			case DrawPrimitiveTriangles:
				prim = GL_TRIANGLES;
				mat = mTriangleMaterial;
				// glEnable(GL_CULL_FACE); // culling valid for triangles, but optional
				break;
			default:
				assert(false);
				return;
			};

			const Eigen::Matrix4f viewProjT = cameraParam.viewProj.transpose();
			m_overlayGl.Draw(*mat, prim, drawList.primType == DrawPrimitiveLines,
				drawList.vertexData, drawList.vertexCount, sizeof(VertexFormat),
				viewProjT.data(), cameraParam.viewportWidth, cameraParam.viewportHeight);
			mat->Unbind();

		}

		// Custom effects of this channel: depth tested like the primitives above, before the
		// view-cube sub-viewport is set up below.
		drawEffects(EImChannel::Sorted);

		//draw lit vertex data
		{
			// The labels are generated at runtime (language-aware), so a language or theme switch
			// only marks them dirty; the upload happens here, where the context is current.
			// The runtime label atlas is generated (see ViewCubeLabels), but the cube maps only a
			// sub-rectangle of each cell onto the flat centre of a face, and the generated layout
			// does not line up with it yet: the baked atlas stays in use until the face UV
			// rectangles are taken from the geometry instead of assumed to be whole cells.
			if (MOON::ViewCubeLabels::IsDirty()) {
				MOON::UpdateViewCubeLabels();
			}
			auto& viewCube = NavigateCube();
			float boxExtent=(viewCube.maxConner - viewCube.minConner).norm();
			Eigen::Vector3f boxECenter = (viewCube.maxConner + viewCube.minConner) / 2.0;
			Maths::FVector3 boxCenter = { boxECenter.x(),boxECenter.y(),boxECenter.z()};

			const ViewCubeLayout cubeLayout = ComputeViewCubeLayout(
				cameraParam.viewportWidth,
				cameraParam.viewportHeight
			);
			viewCube.screenPos.startX = cubeLayout.glViewportX;
			viewCube.screenPos.startY = cubeLayout.glViewportY;
		    int viewPortX = viewCube.screenPos.startX;
			int viewPortY =  viewCube.screenPos.startY;
			float u = 2*(cameraParam.cursor.x() - viewPortX) / (float)viewCube.screenPos.viewportSizeX -1;
			float v = 2*(cameraParam.viewportHeight -cameraParam.cursor.y() - viewPortY) / (float)viewCube.screenPos.viewportSizeY -1;

			auto transform=renderView->GetCamera()->GetTransform();
			auto forward=transform.GetWorldForward();
			auto position = -forward * boxExtent / 2.0+boxCenter;
			auto up = transform.GetWorldUp();
			auto view = Maths::FMatrix4::CreateView(
				position.x, position.y, position.z,
				position.x+forward.x, position.y+forward.y, position.z+forward.z,
				up.x, up.y, up.z
			);
		
			auto proj=Maths::FMatrix4::CreateOrthographic(boxExtent/2.0, 1, 0.1, boxExtent);
			int faceIndex=viewCube.hit(ToEigenMatrix4f(proj * view),u,v);
			// The rotate buttons are drawn on top of the cube: while the cursor
			// sits on one of them the click belongs to the widget, so the cube
			// neither highlights a cell nor fits the view. Asking the overlay
			// registry keeps this working for any future screen widget.
			if (ScreenOverlayRegistry::Instance().HitsShape(
				cameraParam.cursor.x(),
				cameraParam.cursor.y()))
			{
				faceIndex = -1;
			}
			if (faceIndex != mViewCubeHoveredCell) {
				// -1 resets every cell, so leaving the cube clears the highlight.
				viewCube.setCellColor(faceIndex,{255,255,0,255});
				mViewCubeHoveredCell = faceIndex;
			}
			if (faceIndex != -1 && wasKeyPressed(MouseLeft) && renderView != nullptr) {
				auto nor=-viewCube.getCellNormal(faceIndex);
				const Maths::FVector3 fitDirection{ nor.x(),nor.y(),nor.z() };
				// Fit the selection when there is one, the scene otherwise - and the
				// scene again if the selection has nothing to frame (a picked face is a
				// topology leaf whose mesh lives on the actor that batches it, for
				// example). The cube has to answer every click.
				renderView->FitToFocus(fitDirection);
			}
			mCellMaterial->SetFeatures({ "WITH_EDGE","CUSTOM_PROJECT","CUSTOM_VIEWPORT"});
			mCellMaterial->SetProperty("uModelMatrix", ToFMatrix4(viewCube.model));
			mCellMaterial->SetProperty("uViewMatrix", view);
			mCellMaterial->SetProperty("uVProjMatrix", proj);
			mCellMaterial->SetProperty("u_AlbedoMap",viewCube.texture);
			mCellMaterial->SetProperty("edgeTexture", viewCube.edgeTexture);
			mCellMaterial->SetProperty("uViewPortSize", Maths::FVector2(viewCube.screenPos.viewportSizeX, viewCube.screenPos.viewportSizeY));
			mCellMaterial->SetProperty("uViewPortStart", Maths::FVector2(viewCube.screenPos.startX, viewCube.screenPos.startY));
			mCellMaterial->Bind(&mEmptyTexture2D, &mEmptyTextureCube);
			mCellMaterial->UploadProperties(true, &mEmptyTexture2D, &mEmptyTextureCube);
			viewCube.bind();
			m_overlayGl.DrawBoundTriangles(viewCube.numVertex,
				viewPortX, viewPortY, viewCube.screenPos.viewportSizeX, viewCube.screenPos.viewportSizeY);
			auto& viewAxis = CoordAxis();
			mCellMaterial->SetProperty("uModelMatrix", ToFMatrix4(viewAxis.model));
			mCellMaterial->SetProperty("edgeTexture", viewAxis.edgeTexture);
			mCellMaterial->Bind(&mEmptyTexture2D, &mEmptyTextureCube);
			mCellMaterial->UploadProperties(true, &mEmptyTexture2D, &mEmptyTextureCube);
			viewAxis.bind();
			m_overlayGl.DrawBoundTriangles(viewAxis.numVertex);
			{

			}
		}
	}
	void ImRenderer::drawLit()
	{
		if (litVertexArray.empty() || mLitMaterial == nullptr)
		{
			litVertexArray.clear();
			return;
		}

		auto* driver = Core::Global::ServiceLocator::Get<Editor::Core::Context>().driver.get();
		// The shaded faces belong to the scene: the same depth-tested, blended state the widget
		// meshes use (reverse-Z greater-equal), so world geometry in front of a handle hides it.
		// drawSort() leaves the navigation cube's sub-viewport behind, hence the explicit restore.
		m_overlayGl.SetViewport(0, 0, static_cast<int>(cameraParam.viewportWidth),
			static_cast<int>(cameraParam.viewportHeight));
		m_overlayGl.BeginMeshes(*driver, true);
		// 相机位置与屏幕基向量：GizmoLit 的灯架跟着相机走（屏幕左上主光），
		// 这样绕到任何视角，同一块手柄的亮度都不会从很亮跌到很暗。
		const Eigen::Vector3f cameraRight(cameraParam.inverseView(0, 0), cameraParam.inverseView(1, 0),
			cameraParam.inverseView(2, 0));
		const Eigen::Vector3f cameraUp(cameraParam.inverseView(0, 1), cameraParam.inverseView(1, 1),
			cameraParam.inverseView(2, 1));
		mLitMaterial->SetProperty("u_ViewPos",
			Maths::FVector3{ cameraParam.eye.x(), cameraParam.eye.y(), cameraParam.eye.z() });
		mLitMaterial->SetProperty("u_CameraRight",
			Maths::FVector3{ cameraRight.x(), cameraRight.y(), cameraRight.z() });
		mLitMaterial->SetProperty("u_CameraUp",
			Maths::FVector3{ cameraUp.x(), cameraUp.y(), cameraUp.z() });
		const Eigen::Matrix4f viewProjT = cameraParam.viewProj.transpose();
		m_overlayGl.Draw(*mLitMaterial, GL_TRIANGLES, false,
			litVertexArray.data(), litVertexArray.size(), sizeof(VertexFormat),
			viewProjT.data(), cameraParam.viewportWidth, cameraParam.viewportHeight);
		mLitMaterial->Unbind();

		litVertexArray.clear();
	}
	void ImRenderer::drawMesh()
	{
		auto* driver = Core::Global::ServiceLocator::Get<Editor::Core::Context>().driver.get();
		// The pipeline state the widget meshes draw with is the render module's.
		m_overlayGl.BeginMeshes(*driver, true);
		mCellMaterial->SetFeatures({"WITH_EDGE","FIXED_SCALE","BLOCK_COLOR"});
		for (auto& drawMesh : drawMeshList)
		{
			auto polygon = mPreStorePolygon[drawMesh.mesh];
			mCellMaterial->SetProperty("uModelMatrix", ToFMatrix4(drawMesh.model));
			mCellMaterial->SetProperty("edgeTexture", polygon->edgeTexture);
			mCellMaterial->SetProperty("u_AlbedoMap",polygon->texture);
				mCellMaterial->SetProperty("blockTexture", polygon->blockTexture);
				mCellMaterial->Bind(&mEmptyTexture2D, &mEmptyTextureCube);
				mCellMaterial->UploadProperties(true, &mEmptyTexture2D, &mEmptyTextureCube);
				polygon->bind();
			m_overlayGl.DrawBoundTriangles(polygon->numVertex);
		}
	}

	void ImRenderer::drawMeshPick()
	{
		auto* driver = Core::Global::ServiceLocator::Get<Editor::Core::Context>().driver.get();
		// The picking mesh pass draws with blending off; the state is the render module's.
		m_overlayGl.BeginMeshes(*driver, false);
		mCellMaterial->SetFeatures({ "WITH_EDGE","FIXED_SCALE","BLOCK_COLOR" });
		
		for (auto& drawMesh : drawMeshList)
		{
			auto polygon = mPreStorePolygon[drawMesh.mesh];
			mCellMaterial->SetProperty("uModelMatrix", ToFMatrix4(drawMesh.model));
			mCellMaterial->SetProperty("edgeTexture", polygon->edgeTexture);
			mCellMaterial->SetProperty("u_AlbedoMap", polygon->texture);
			mCellMaterial->SetProperty("blockTexture", polygon->blockTexture);
			mCellMaterial->SetProperty("polygonId", polygon->getId());
			mCellMaterial->Bind(&mEmptyTexture2D, &mEmptyTextureCube, "PICKING_PASS");
			mCellMaterial->UploadProperties(true, &mEmptyTexture2D, &mEmptyTextureCube);
			
			polygon->bind();
			m_overlayGl.DrawBoundTriangles(polygon->numVertex);
		}
	}


	void ImRenderer::endFrame()
	{
		runDrawTask();
		drawWidgets();
		test();	

		assert(!endFrameCalled);
		endFrameCalled = true;
		drawMesh();
		drawSort();
		// 光照三角形夹在"会被场景遮挡"的排序通道与"永远在最上层"的无排序通道之间：
		// 它们像场景几何一样被深度遮挡，而随后画的手柄线/点仍然压在它们上面。
		drawLit();
		drawUnsort();
		drawText();
	}
	void ImRenderer::newImgui()
	{
		if (isBeginImgui) {
			return;
		}
		isBeginImgui = true;
		QtImGui::newFrame(imref);
		ImPlot::SetCurrentContext(ctx);
	}
	void ImRenderer::endImgui()
	{
		ImGui::Render();
		QtImGui::render(imref);
		isBeginImgui = false;
	}
	void ImRenderer::clear()
	{
		//to do something here.
	}
	void ImRenderer::updateDepth(float _depth)
	{
		hotDepth = _depth;
	}
	bool ImRenderer::makeHot(unsigned int _id, float _depth, bool _intersects)
	{
		if (activeId == IdInvalid && _depth < hotDepth && _intersects && !isKeyDown(ActionSelect))
		{
			hotId = _id;
			appHotId = appId;
			hotDepth = _depth;
			return true;
		}
		return false;
	}
	bool ImRenderer::makeHot2D(unsigned int id)
	{
		hotId2D = id;
		return true;
	}
	bool ImRenderer::isHot(unsigned int id)
	{
		return id == hotId;
	}
	bool ImRenderer::isHot2D(unsigned int id)
	{
		return id == hotId2D;
	}
	bool ImRenderer::isAppActive(unsigned int id)
	{
		return appActiveId == id;
	}
	void ImRenderer::makeActive(unsigned int id)
	{
		activeId = id;
		appActiveId = id == IdInvalid ? IdInvalid : appId;
	}
	void ImRenderer::makeActive2D(unsigned int _id)
	{
		activeId2D = _id;
	}
	void ImRenderer::resetId()
	{
		activeId = hotId = appActiveId = appHotId = IdInvalid;
		hotDepth = FLT_MAX;
	}
	void ImRenderer::resetId2D()
	{
		activeId2D = hotId2D = IdInvalid;
	}
	unsigned int ImRenderer::makeId(const char* str)
	{
		unsigned int ret = idStack.size();
		while (*str)
		{
			ret ^= (unsigned int)*str++;
			ret *= kFnv1aPrime32;
		}
		return ret;
	}
	void ImRenderer::pushEnableSorting(bool enable)
	{
		assert(primMode == PrimitiveModeNone); // can't change sort mode mid-primitive
		vertexDataIndex = enable ? 1 : 0;
		enableSortingStack.push_back(enable);
	}
	void ImRenderer::popEnableSorting()
	{
		assert(primMode == PrimitiveModeNone); // can't change sort mode mid-primitive
		enableSortingStack.pop_back();
		vertexDataIndex = enableSortingStack.back() ? 1 : 0;
	}
	void ImRenderer::setEnableSorting(bool _enable)
	{
		assert(primMode == PrimitiveModeNone); // can't change sort mode mid-primitive
		vertexDataIndex = _enable ? 1 : 0;
		enableSortingStack.back() = _enable;
	}
	void ImRenderer::setEnableLit(bool enable)
	{
		enableLit = enable;
		if (!enableLitStack.empty())
		{
			enableLitStack.back() = enable;
		}
	}
	void ImRenderer::pushEnableLit(bool enable)
	{
		assert(primMode == PrimitiveModeNone); // can't change lit mode mid-primitive
		enableLitStack.push_back(enable);
		enableLit = enable;
	}
	void ImRenderer::popEnableLit()
	{
		assert(primMode == PrimitiveModeNone); // can't change lit mode mid-primitive
		assert(enableLitStack.size() > 1);
		enableLitStack.pop_back();
		enableLit = enableLitStack.back();
	}
	void ImRenderer::pushLayerId(unsigned int layer)
	{
		assert(primMode == PrimitiveModeNone); // can't change layer mid-primitive
		int idx = findLayerIndex(layer);
		if (idx == -1) // not found, push new layer
		{
			idx = layerIdMap.size();
			layerIdMap.push_back(layer);
			for (int i = 0; i < DrawPrimitiveCount; ++i)
			{
				vertexData[0].push_back(new VertexList());
				*(vertexData[0].back()) = std::vector<VertexFormat>();
				vertexData[1].push_back(new VertexList());
				*(vertexData[1].back()) = std::vector<VertexFormat>();
			}
		}
		layerIdStack.push_back(layer);
		layerIndex = idx;
	}
	void ImRenderer::popLayerId()
	{
		assert(layerIdStack.size() > 1);
		layerIdStack.pop_back();
		layerIndex = findLayerIndex(layerIdStack.back());
	}
	ImRenderer::~ImRenderer()
	{
		for (int i = 0; i < vertexData[0].size(); i++)
		{
			delete vertexData[0][i];
			delete vertexData[1][i];
		}
		terminate();
	}
	float ImRenderer::pixelsToWorldSize(const Eigen::Vector3f& position, float pixels)
	{
		float d = cameraParam.orthProj ? 1.0f : (position - cameraParam.eye).norm();
		return cameraParam.projectY * d * (pixels / cameraParam.viewportHeight);
	}
	Eigen::Vector3f ImRenderer::screenToWorld(const Eigen::Vector2f& pos)
	{
		Eigen::Vector3f p = { 2.0f * pos.x() / cameraParam.viewportWidth - 1.0f, 2.0f * (1.0f - pos.y() / cameraParam.viewportHeight) - 1.0f, 0.0f };
		return MatrixMulPoint(cameraParam.viewProj.inverse(), p);
	}
	void ImRenderer::runDrawTask()
	{
		for (auto& drawTask : mDrawTaskMap)
		{
			drawTask.second();
		}
		for (auto& item : cancelList)
		{
			if (mDrawTaskMap.find(item) != mDrawTaskMap.end())
			{
				mDrawTaskMap.erase(item);
			}
		}
		cancelList.clear();
	}
	void ImRenderer::drawWidgets()
	{
		// Drawn in the order the widgets ask for (see EventWidget::setDrawOrder):
		// a tool that paints over the sketch it edits has to say so, because the
		// order the container hands its widgets out in is not one - two primitives
		// drawn on the same pixels are decided by which of them was pushed last.
		std::vector<EventWidget*> widgets;
		widgets.reserve(mGizmoWidgets.size());
		for (const auto& it : mGizmoWidgets) {
			widgets.push_back(it.second);
		}
		std::stable_sort(widgets.begin(), widgets.end(),
			[](const EventWidget* a, const EventWidget* b) {
				return a->getDrawOrder() < b->getDrawOrder();
			});
		for (EventWidget* widget : widgets) {
			widget->update();
		}
	}
	void ImRenderer::placeDrawTask(const std::string& name, std::function<void()> task)
	{
		if (mDrawTaskMap.find(name) != mDrawTaskMap.end())
		{
			mDrawTaskMap.erase(name);
		}
		mDrawTaskMap.emplace(name, task);
	}

	void ImRenderer::cancleDrawTask(const std::string& name)
	{
		cancelList.push_back(name);
	}

	void ImRenderer::addGizmoWidget(EventWidget* widget)
	{
		if (mGizmoWidgets.find(widget->getWidgetId()) != mGizmoWidgets.end()) {
			assert(false&&"the widget has been added!");
		}
		mGizmoWidgets[widget->getWidgetId()]=widget;
	}

	void ImRenderer::removeGizmoWidget(EventWidget* widget)
	{
		if (widget) {
			mGizmoWidgets.erase(widget->getWidgetId());
		}
	}

	EventWidget* ImRenderer::getGizmoWidget(const std::string& name)
	{
		int cnt = 0;
		EventWidget* ptr = nullptr;
		for (auto& it : mGizmoWidgets) {
			if (it.second->getName() == name) {
				cnt++;
				ptr=it.second;
			}
		}
		if (cnt > 1) {
			assert(false && "there are more than one widget with the same name!");
		}
		return ptr;
	}

	void ImRenderer::pushAlpha(float value)
	{
		alphaStack.push_back(value);
	}

	void ImRenderer::popAlpha()
	{
		alphaStack.pop_back();
	}

	void ImRenderer::pushColor(const Eigen::Vector4<uint8_t>& color)
	{
		colorStack.push_back(color);
	}

	void ImRenderer::popColor()
	{
		colorStack.pop_back();
	}

	void ImRenderer::pushSize(float value)
	{
		sizeStack.push_back(value);
	}

	void ImRenderer::popSize()
	{
		sizeStack.pop_back();
	}

	void ImRenderer::pushMatrix(const Eigen::Matrix4f& mat)
	{
		matrixStack.push_back(mat);
	}

	void ImRenderer::popMatrix()
	{
		matrixStack.pop_back();
	}

	int ImRenderer::findLayerIndex(unsigned int _id) const
	{
		for (int i = 0; i < (int)layerIdMap.size(); ++i)
		{
			if (layerIdMap[i] == _id)
			{
				return i;
			}
		}
		return -1;
	}
	VertexList* ImRenderer::getCurrentVertexList()
	{
		return vertexData[vertexDataIndex][layerIndex * DrawPrimitiveCount + primType];
	}

	VertexFormat* ImRenderer::lastVertex()
	{
		if (primMode == PrimitiveModeNone)
		{
			// 没有打开的图元，当前桶可能指向上一段用的那个列表，最后一个顶点不是"刚写进去的"。
			return nullptr;
		}
		VertexList* vertexList = getCurrentVertexList();
		return vertexList->empty() ? nullptr : &vertexList->back();
	}
}
