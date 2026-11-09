#pragma once
#include <math.h>
#include <Eigen/Core>
#include <vector>
#include <unordered_map>
#include "Interactive/Im3DType.h"
#include "Interactive/Im3DEffect.h"
#include "Rendering/Resources/Texture.h"
#include <Rendering/Overlay/Im3DGLRenderer.h>
namespace Editor {
	namespace Panels {
		class SceneView;
	}
}
namespace Rendering::Data
{
	class Material;
}
namespace MOON
{
	using Vec2 = Eigen::Vector2f;
	/** 无效的交互 id：没有手柄处于 hot / active 时用它重置。 */
	inline constexpr unsigned int IdInvalid = 0;
	class EventWidget;
	
	class ImRenderer
	{
	private:
		ImRenderer();
		~ImRenderer();
		ImRenderer(const ImRenderer&) = delete;
		ImRenderer& operator=(const ImRenderer&) = delete;
	public:
		static ImRenderer& instance();
		void init();
		/** The gizmo geometry's GL objects (vertex array, vertex buffer, materials).
		 *
		 * Separate from init() because of which context each half belongs to: the gizmo pass draws
		 * inside the render module's frame, on the module's own context, while ImGui draws after
		 * that frame has been pasted, on the viewport's context. Vertex arrays are per context, so
		 * this half has to be created on the module's, and init() on the viewport's. */
		void initGizmoGl();
		void preStoreMesh();
		void prepareGl();
		void registerDebugSettings();
		void terminate();
		void selectPolygon(int pid,int bid);
		bool isSelectPolygon(const std::string&pname,const std::string& bname);
		void resetSelectPolygon();
		void begin(PrimitiveMode _mode);
		void end();
		void vertex(const Eigen::Vector3f& p, const Eigen::Vector4f& c, const Eigen::Vector3f& n, const Eigen::Vector2f& tex);
		void vertex(const Eigen::Vector3f& p, const Eigen::Vector4<uint8_t>& c, const Eigen::Vector3f& n, const Eigen::Vector2f& tex);
		void vertex(const Eigen::Vector3f& _position, float _size, const Eigen::Vector4<uint8_t>& _color);
		void vertex(const Eigen::Vector3f& _position)
		{
			vertex(_position, sizeStack.back(), colorStack.back());
		}
		void vertex(const Eigen::Vector3f& _position, Eigen::Vector4<uint8_t> _color)
		{
			vertex(_position, sizeStack.back(), _color);
		}
		//void draw
		void drawOneMesh(const Eigen::Vector3f& translation, const Eigen::Matrix3f& rotation, const Eigen::Vector3f& scale, const std::string& mesh, bool longterm = false);
		void drawOneMesh(Eigen::Vector3f& translation, Eigen::Matrix3f& rotation, Eigen::Vector3f& scale,
			Eigen::Vector3f& color,
			const std::string& mesh, bool longterm = false);
		void drawOneMesh(
			Eigen::Vector3f translation, Eigen::Vector3f scale, const std::string& mesh, bool longterm = false);
		void drawOneMesh(
			Eigen::Vector3f translation, Eigen::Vector3f scale, Eigen::Vector3f& color, const std::string& mesh, bool longterm = false);
		void drawOneMesh(const Eigen::Matrix4f& model, const std::string& mesh, bool longterm = false);
		void drawOneMesh(const Eigen::Matrix4f& model, const std::string& mesh, Eigen::Vector3f& color, bool longterm = false);
		void drawOneFixScaleMesh(
			Eigen::Matrix4f& model, const std::string& mesh, Eigen::Vector3f& color, bool longterm = false);
		void drawPoint(const Eigen::Vector3f& _position, float _size, Eigen::Vector4<uint8_t> _color);
		void drawPoint(const Eigen::Vector3f& pos);
		void drawPoint(const Eigen::Vector3f& pos, float size);
		void drawPointList(const std::vector<Eigen::Vector3f>& position, float size, Eigen::Vector4<uint8_t> color);
		void drawLineList(const std::vector<Eigen::Vector3f>& position, float _size, Eigen::Vector4<uint8_t> _color);

		/** Draws UTF-8 text in world space. p_right and p_up span the glyph quads, so the caller
		 * chooses the orientation (screen facing or lying on a sketch plane) and the size.
		 * p_position is the baseline start. Text goes into its own vertex list and is drawn with
		 * its own material; the point/line/triangle lists and their passes are untouched. */
		void drawText3D(const Eigen::Vector3f& p_position, const char* p_text, float p_pixelSize,
			Eigen::Vector4<uint8_t> p_color,
			const Eigen::Vector3f& p_right, const Eigen::Vector3f& p_up,
			float p_worldPerPixel = 1.0f);

		/** Screen facing text whose glyphs stay at p_pixelSize pixels whatever the zoom: the basis
		 * comes from the camera and the world scale from the projection at p_position. This is
		 * what labels (dimensions, sketch annotations) use. */
		void drawText3D(const Eigen::Vector3f& p_position, const char* p_text, float p_pixelSize,
			Eigen::Vector4<uint8_t> p_color, bool p_overlay = false);

		/** Text anchored to a world point but nudged by an offset given in screen pixels - what
		 * annotations need, since their layout (dragging, avoiding geometry) is authored in
		 * screen space while the anchor is a point on the sketch plane. The offset is applied
		 * with the camera basis, so it keeps its pixel meaning under any view. */
		void drawLabel3D(const Eigen::Vector3f& p_anchor, const char* p_text, float p_pixelSize,
			Eigen::Vector4<uint8_t> p_color, float p_offsetXPx, float p_offsetYPx, bool p_overlay = true);

		/** The same, but the glyphs are laid along p_basisRight/p_basisUp instead of the camera's
		 * axes - text that belongs to a plane (a sketch annotation) and has to turn with it when
		 * the view rotates, rather than staying screen aligned. */
		void drawLabel3D(const Eigen::Vector3f& p_anchor, const char* p_text, float p_pixelSize,
			Eigen::Vector4<uint8_t> p_color, float p_offsetXPx, float p_offsetYPx,
			const Eigen::Vector3f& p_basisRight, const Eigen::Vector3f& p_basisUp, bool p_overlay = true);

		/** Draws a texture as a camera facing quad of p_pixelSize pixels, centred on
		 * p_position. Same material and vertex list as the text: the only difference
		 * between a glyph and an icon is which texture the run samples. */
		void drawIcon3D(const Eigen::Vector3f& p_position,
			::Rendering::Resources::Texture* p_texture, float p_pixelSize,
			Eigen::Vector4<uint8_t> p_color, bool p_overlay = true);

		/** The same quad laid along p_basisRight/p_basisUp instead of the camera's axes, and
		 * nudged by a pixel offset - an icon that belongs to a plane (a sketch constraint marker)
		 * has to turn with it. */
		void drawIcon3D(const Eigen::Vector3f& p_position,
			::Rendering::Resources::Texture* p_texture, float p_pixelSize,
			Eigen::Vector4<uint8_t> p_color,
			const Eigen::Vector3f& p_basisRight, const Eigen::Vector3f& p_basisUp,
			float p_offsetXPx = 0.0f, float p_offsetYPx = 0.0f, bool p_overlay = true);

		/** World-space lines drawn with a dash pattern by tessellating them on the CPU. Kept for
		 * the callers that have not moved to the LineDash effect yet (see Im3DEffect.h and
		 * Interactive/Effect/LineDashEffect.h); the CPU path re-splits every frame.
		 * The endpoints stay world space, so a dashed curve goes through exactly the pipeline a
		 * solid one does (same depth test, size and colour stacks); only the dash rhythm is
		 * measured in screen pixels, which keeps the pattern size constant under zoom.
		 * p_position holds endpoint pairs; p_phase lets a strip continue the pattern across its
		 * segments. */
		void drawDashedLineList(const std::vector<Eigen::Vector3f>& p_position, float p_size,
			Eigen::Vector4<uint8_t> p_color, float p_dashPx, float p_gapPx, float p_phase = 0.0f);

		/** Dashed version of a continuous polyline (a strip of points, not pairs). */
		void drawDashedPolyline(const std::vector<Eigen::Vector3f>& p_points, float p_size,
			Eigen::Vector4<uint8_t> p_color, float p_dashPx, float p_gapPx, float p_phase = 0.0f);

		/** Same, with the current size and colour. */
		void drawDashedPolyline(const std::vector<Eigen::Vector3f>& p_points,
			float p_dashPx, float p_gapPx, float p_phase = 0.0f);

		// ---- custom drawing effects ------------------------------------------------------
		//
		// The built-in primitives are drawn with one material per primitive type; an effect is a
		// self-contained drawing unit that consumes a run of vertices and knows how to draw them.
		// The renderer only knows AImEffect: it collects the vertices and hands each run to the
		// effect, which binds its own material, applies its own settings and issues the draw. The
		// concrete looks (dash patterns, colour ramps, glow, ...) never appear here.

		/** Registers an effect; the renderer takes ownership. The effect builds its own material,
		 * so this needs the GL context and belongs where the built-in materials are created
		 * (ImRenderer::PrepareGl). */
		AImEffect* registerEffect(std::unique_ptr<AImEffect> p_effect);

		AImEffect* findEffect(const std::string& p_id) const;
		template <class T>
		T* findEffectAs(const std::string& p_id) const { return dynamic_cast<T*>(findEffect(p_id)); }
		void drawTriangleList(const std::vector<Eigen::Vector3f>& position, float _size, Eigen::Vector4<uint8_t> _color);
		/** 带法线的三角形列表：normals 与 position 一一对应（通常同一个三角形的三个顶点共用
		 * 一条面法线）。法线由调用方在构造几何时算好（WidgetViewData 的 TriangleFace 在
		 * setTriangleFace 里算），绘制期不再求叉积。光照打开时写进 litVertexArray，
		 * 由 drawLit() 用 GizmoLit.shader 绘制；否则退回普通三角形通道。 */
		void drawTriangleList(const std::vector<Eigen::Vector3f>& position,
			const std::vector<Eigen::Vector3f>& normals, float _size, Eigen::Vector4<uint8_t> _color);
		void drawLine(const Eigen::Vector3f& _a, const Eigen::Vector3f& _b, float _size, Eigen::Vector4<uint8_t> _color);
		void drawLine(const Eigen::Vector3f& _a, const Eigen::Vector3f& _b, float _size);
		void drawLine(const Eigen::Vector3f& _a, const Eigen::Vector3f& _b);
		
		void drawTriangle(const Eigen::Vector3f& a, const Eigen::Vector3f& b, const Eigen::Vector3f& c, const Eigen::Vector3f& n);
		void drawTriangle(const Eigen::Vector3f& _a, const Eigen::Vector3f& _b, const Eigen::Vector3f& _c);
		void drawTriangle(const Eigen::Vector3f& _a, const Eigen::Vector3f& _b, const Eigen::Vector3f& _c, const Eigen::Vector4<uint8_t>& _color);
		void drawQuadFilled(
			const Eigen::Vector3f& _origin, const Eigen::Vector3f& _normal, const Eigen::Vector2<float>& _size);
		void drawQuad(
			const Eigen::Vector3f& _a, const Eigen::Vector3f& _b, const Eigen::Vector3f& _c, const Eigen::Vector3f& _d);
		void drawQuad(const Eigen::Vector3f& _origin, const Eigen::Vector3f& _normal, const Eigen::Vector2<float>& _size);
		void drawQuadFilled(
			const Eigen::Vector3f& _a, const Eigen::Vector3f& _b, const Eigen::Vector3f& _c, const Eigen::Vector3f& _d);
		void drawArrow(const Eigen::Vector3f& _start, const Eigen::Vector3f& _end, float _headLength, float axisThickness,
			float _headThickness);
		void drawArrow(const Eigen::Vector3f& _start, const Eigen::Vector3f& _end, float _headLength = -1.0f,
			float _headThickness = -1.0f);
		void drawCircleFilled(
			const Eigen::Vector3f& _origin, const Eigen::Vector3f& _normal, float _radius, int _detail = -1);
		void drawTransparencyCircle(
			const Eigen::Vector3f& _origin, const Eigen::Vector3f& _normal, float _radius, int _detail = -1);
		void drawSphere(const Eigen::Vector3f& _origin, float _radius, int _detail = -1);
		void drawSphereFilled(const Eigen::Vector3f& _origin, float _radius, int _detail = -1);
		void drawCircleFaceCamera(const Eigen::Vector3f& _origin);
		void drawCircle(const Eigen::Vector3f& _origin, const Eigen::Vector3f& _normal, float _radius, int _detail = -1);
		
		void drawConeFilled(
			const Eigen::Vector3f& _origin, const Eigen::Vector3f& _normal, float height, float _radius, int _detail);
		void drawAlignedBox(const Eigen::Vector3f& _min, const Eigen::Vector3f& _max);
		void drawAlignedBoxFilled(const Eigen::Vector3f& _min, const Eigen::Vector3f& _max);
		void drawAlignedBoxFilled(
			const Eigen::Vector3f& _min, const Eigen::Vector3f& _max, const std::vector<Eigen::Vector4<uint8_t>>& color);
		void drawPlaneGrid(const Eigen::Vector3f& origin, const Eigen::Vector3f& normal, float scale, float size);
		int  estimateLevelOfDetail(const Eigen::Vector3f& _position, float _worldSize, int _min = 4, int _max = 256);
		Eigen::Vector2f worldToScreen(const Eigen::Vector3f& pos);
		FrameParam& getFrameParam();
		void test();
		void drawUnsort();
		void drawSort();
		/** Uploads the text vertices and draws them, one pass per atlas. */
		void drawText();
		/** Uploads and draws the shaded triangles (litVertexArray) with the lit material.
		 * 搬进来的那批三角形顶点不带法线：法线在 drawTriangleList 里按三角形现算成面法线，
		 * 所以 widget 不需要自己构造法线。 */
		void drawLit();
		void drawMesh();
		void drawMeshPick();
		void newFrame(Editor::Panels::SceneView* view);
		void endFrame();
		void newImgui();
		void endImgui();
		void clear();
		void updateDepth(float _depth);
		bool makeHot(unsigned int _id, float _depth, bool _intersects);
		bool makeHot2D(unsigned int id);
		bool isHot(unsigned int _id);
		bool isHot2D(unsigned int id);
		bool isAppActive(unsigned int _id);
		void makeActive(unsigned int _id);
		void makeActive2D(unsigned int _id);
		void resetId();
		void resetId2D();
		/** hot / active 仲裁的只读面：操纵器（Interactive/Gizmo）靠这几个接口判断
		 * "这个手柄是不是我"和"能不能抢焦点"。 */
		unsigned int getActiveId() const { return activeId; }
		unsigned int getHotId() const { return hotId; }
		unsigned int getActiveId2D() const { return activeId2D; }
		unsigned int getHotId2D() const { return hotId2D; }
		unsigned int getAppHotId() const { return appHotId; }
		/** 设置本帧正在绘制/交互的 app id（makeHot 用它记录 hot 属于哪个 app）。 */
		void setAppId(unsigned int _id) { appId = _id; }
		/** 当前 hot 的手柄是否属于本 app —— 即交互是否还停在本次拖拽上。 */
		bool isAppHot() const { return appHotId == appId; }
		unsigned int makeId(const char* _str);
		void setId(unsigned int id)
		{
			idStack.back() = id;
		}
		unsigned int getId() const
		{
			return idStack.back();
		}
		void pushId(unsigned int id)
		{
			idStack.push_back(id);
		}
		void popId()
		{
			assert(idStack.size() > 1);
			idStack.pop_back();
		}
		void pushEnableSorting(bool enable);
		void popEnableSorting();
		void setEnableSorting(bool enable);
		/** 打开后，drawTriangleList 的三角形改走光照通道（litVertexArray + drawLit）。
		 * 线段与点永远不带光照：它们走的是原来的顶点桶。 */
		void setEnableLit(bool enable);
		void pushEnableLit(bool enable);
		void popEnableLit();
		bool getEnableLit() const
		{
			return enableLit;
		}
		bool getEnableSorting() const
		{
			return enableSortingStack.back();
		}

		unsigned int getLayerId() const
		{
			return layerIdStack.back();
		}
		void pushLayerId(unsigned int layer);
		void popLayerId();

		float pixelsToWorldSize(const Eigen::Vector3f& position, float pixels);
		Eigen::Vector3f screenToWorld(const Eigen::Vector2f& pos);

		void runDrawTask();
		void drawWidgets();
		void placeDrawTask(const std::string& name, std::function<void()> task);
		void cancleDrawTask(const std::string& name);
		void addGizmoWidget(EventWidget* widget);
		void removeGizmoWidget(EventWidget* widget);
		EventWidget* getGizmoWidget(const std::string&name);
		std::unordered_map<unsigned int, EventWidget*>& getGizmoWidgets() {
			return mGizmoWidgets;
		}
		template<class T>
		T* getGizmoWidgetAS(const std::string& name) {
			return dynamic_cast<T*>(getGizmoWidget(name));
		}
		bool isKeyDown(KeyAction key) const
		{
			return keyDownCurr[key];
		}
		bool wasKeyPressed(KeyAction key) const
		{
			return keyDownCurr[key] && !keyDownPrev[key];
		}
		bool wasKeyReleased(KeyAction key) const
		{
			return !keyDownCurr[key] && keyDownPrev[key];
		}

		void pushAlpha(float value);
		void popAlpha();
		void pushColor(const Eigen::Vector4<uint8_t>& color);
		void popColor();
		void pushSize(float value);
		void popSize();
		void pushMatrix(const Eigen::Matrix4f& mat);
		void popMatrix();
		/** 状态栈的只读面：操纵器要在自己压入的局部坐标系里画图/读当前透明度。 */
		const Eigen::Matrix4f& getMatrix() const { return matrixStack.back(); }
		float getAlpha() const { return alphaStack.back(); }
		/** The current size / colour stack tops: what the line and point primitives would use, so
		 * an effect submitting on behalf of a caller picks up the same pen. */
		float getSize() const { return sizeStack.back(); }
		const Eigen::Vector4<uint8_t>& getColor() const { return colorStack.back(); }
		/** 本帧当前顶点桶里最后写入的顶点，供操纵器就地改 alpha（例如旋转环被
		 * 球体挡住的部分）。桶为空或不在图元中间时返回 nullptr。 */
		VertexFormat* lastVertex();
	private:
		Editor::Panels::SceneView* renderView = nullptr;
		/** Cell currently highlighted by the navigation cube (-1 = none). */
		int mViewCubeHoveredCell = -1;

	private:
		std::vector<std::string> cancelList;
		std::unordered_map<std::string, std::function<void()>> mDrawTaskMap;
		std::unordered_map<unsigned int, EventWidget*>mGizmoWidgets;
		
		Rendering::HAL::Texture mEmptyTexture2D;
		Rendering::HAL::Texture mEmptyTextureCube;
		Rendering::Data::Material* mLineMaterial = nullptr;
		Rendering::Data::Material* mPointMaterial = nullptr;
		Rendering::Data::Material* mTriangleMaterial = nullptr;
		Rendering::Data::Material* mCellMaterial = nullptr;
		Rendering::Data::Material* mLitMaterial = nullptr;
		/** Uploads and draws the geometry; the GL objects and the pipeline state are the render
		 * module's (see Rendering::Overlay::Im3DGLRenderer). What stays here is building it. */
		::Rendering::Overlay::Im3DGLRenderer m_overlayGl;
		std::vector<Eigen::Vector4<uint8_t>> colorStack;
		std::vector<float> alphaStack;
		std::vector<float> sizeStack;
		std::vector<unsigned int> idStack;
		std::vector<bool> enableSortingStack;
		/** 光照开关的作用域栈：drawTriangleList 按栈顶决定走 litVertexArray 还是普通顶点桶。 */
		std::vector<bool> enableLitStack;
		std::vector<unsigned int> layerIdStack;

		std::vector<Eigen::Matrix4f> matrixStack;
		FrameParam cameraParam;
		bool keyDownCurr[KeyCount];
		bool keyDownPrev[KeyCount];

		unsigned int activeId;
		unsigned int hotId;
		unsigned int activeId2D;
		unsigned int hotId2D;
		float hotDepth;
		unsigned int appId;
		unsigned int appActiveId;
		unsigned int appHotId;

		// Primitive state.
		PrimitiveMode primMode;
		DrawPrimitiveType primType;
		unsigned int firstVertThisPrim;
		unsigned int vertCountThisPrim;
		Eigen::Vector3f minVertThisPrim;
		Eigen::Vector3f maxVertThisPrim;
		//unlit
		std::vector<VertexList*> vertexData[2];
		//lit
		VertexList litVertexArray;
		//effects
		/** The effects the renderer owns and calls once per frame (see drawEffects). */
		std::vector<std::unique_ptr<AImEffect>> mEffects;
		/** Set by the overlay overload for the glyphs it emits next. */
		bool pendingTextOverlay = false;
		std::vector<TextVertex> textVertexArray;
		std::vector<TextRun> textRuns;
		std::vector<Rendering::Resources::Texture*> textAtlasTextures;
		Rendering::Data::Material* mTextMaterial = nullptr;
		int vertexDataIndex;
		std::vector<unsigned int> layerIdMap;
		int layerIndex;
		std::vector<DrawList> drawLists;

		//Long-term lasting
		std::vector<PolygonInstance> drawLongTermMeshList;
		//short lasting
		std::vector<PolygonInstance> drawMeshList;
		std::unordered_map<std::string, PolygonMesh*>mPreStorePolygon;
		std::unordered_map<uint8_t,std::string >mPreStorePolygonId;
		int selectPolygonId = -1;
		int selectBlockId= -1;
		
		bool enableLit = false;
		bool sortCalled;
		bool endFrameCalled;
		int findLayerIndex(unsigned int id) const;
		VertexList* getCurrentVertexList();
		/** 带法线的顶点写进 litVertexArray 前，统一套上矩阵栈与全局 alpha。 */
		void appendLitVertex(VertexFormat& vertex, const Eigen::Vector3f& position, const Eigen::Vector3f& normal);
		/** Appends one vertex to the open effect run, base followed by the sticky extra floats. */
		/** Calls Draw on every effect of p_channel. This is the renderer's only relationship
		 * with the effects: it owns them and gives them the frame's draw entry. */
		void drawEffects(EImChannel p_channel);
		const DrawList* getDrawLists() const
		{
			return drawLists.data();
		}
		unsigned int getDrawListCount() const
		{
			return static_cast<unsigned int>(drawLists.size());
		}
	};
}
